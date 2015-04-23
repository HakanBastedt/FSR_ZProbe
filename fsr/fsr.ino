#undef DEBUGUTSKRIFT
#define N_ADC 3
#define PIN11 11
#define EEPROM_POS 1

#include <EEPROM.h>
#include <EnableInterrupt.h>

volatile byte baseValuePulser; // Från EEPROM numera. =20;   // 20 = känslig. 70 = okänslig
#define STARTING_LIMIT 20
// 
// Att sätta baseValuePulser=25 görs med M40 S25
// 
volatile int blinkPulser; // Flagga ifall vi ska blinka inställningen
volatile byte saveEEPROM; // Flagga ifall vi ska spara till EEPROM

volatile int baseValue[N_ADC]; 
volatile int pin11Fail=false;

volatile byte inSensitiveFactor = 1;

int maxspann(int val[])  // Största spannet av ration
{
    int Max = -1;
    int Min = 30000; 
  
    for (int i=0; i < N_ADC; i++) {
      Max = Max < val[i] ? val[i] : Max;
      Min = Min > val[i] ? val[i] : Min;
    }
    return(Max-Min);
}

int minratio(int val[])  // Min value of reading
{
    int Min = 30000; 
  
    for (int i=0; i < N_ADC; i++) {
      Min = Min > val[i] ? val[i] : Min;
    }
    return(Min);
}

void readS(int read_times, int *val)
{ // Average over read_times cycles
  unsigned long ival[N_ADC];
  for (int i=0; i < N_ADC; i++)
    ival[i] = 0;  
  int n=0;
  do {
    n++;
    for (int i=0; i < N_ADC; i++)
      ival[i] += analogRead(A0+i);
  } while (n < read_times);
  for (int i=0; i < N_ADC; i++)  // Medelvärdet
    val[i] = ival[i]/n;  
}

void setBaseValue()
{
  digitalWrite(13, HIGH);    // LED on
  int val[N_ADC];
  readS(100, val); // 1000 => 0.05 seconds. 
  for (int i=0; i < N_ADC; i++)
    baseValue[i] = val[i]; 
  digitalWrite(13, LOW);
}

int touching()
{
   int ratio[N_ADC];
   int val[N_ADC];

   readS(50, val);
   for (int nadc=0; nadc < N_ADC; nadc++)  // Jobbar med ratio av baseValue
       ratio[nadc] = 100*float(val[nadc])/float(baseValue[nadc]);
   
   int spann = maxspann(ratio);  // Hur mycket skiljer det mellan största till minsta värdet
   int Min = minratio(ratio);    // Lägsta värdet.

   // Touch!
   // Limit1 väger ihop träff i mitten mot träff ovanpå en sensor.
   // 3/4*60*(spread/60*1/3+1)
   // spread = 0 => 45     Träff i mitten, alla lika
   // spread = 60 => 60    Träff i mitten
   // Detta är något jag testade fram.
   int Limit1 = inSensitiveFactor*float(100-baseValuePulser)*(1.0-float(spann)/float(100-baseValuePulser)*0.333333);
   
   // Limit två. Tar en "ren" träff. Faktorn 2.5 gäller för att komma på samma nivå som Limit1
   // En kaffekopp = ca 55 ovanpå en sensor
   // En kaffekopp = ca 42 i mitten
   int Limit2 = inSensitiveFactor*2.5*baseValuePulser;
   
#ifdef DEBUGUTSKRIFT
   // Obs att debugutskriften tar för lång tid för att köra live i Kosseln.
   // 
    Serial.print("Base= ");
    Serial.print(baseValue[0]);
    Serial.print(" ");
    Serial.print(baseValue[1]);
    Serial.print(" ");
    Serial.print(baseValue[2]);
    Serial.print(" ");
    Serial.print(val[0]);
    Serial.print(" ");
    Serial.print(val[1]);
    Serial.print(" ");
    Serial.print(val[2]);
    Serial.print(" ");
    Serial.print(ratio[0]);
    Serial.print(" ");
    Serial.print(ratio[1]);
    Serial.print(" ");
    Serial.print(ratio[2]);
    Serial.print(" ");
    Serial.print(Min);
    Serial.print(Min < Limit1 ? " < " : " > ");
    Serial.print(Limit1);
    Serial.print(Min < Limit1 ? " HIT-1 " : " . ");
    Serial.print(spann);
    Serial.print(spann > Limit2 ? " > " : " < ");
    Serial.print(Limit2);
    Serial.print(spann > Limit2 ? " HIT-2 " : " . ");
    Serial.println();
#endif

   if (Min < Limit1) {
     inSensitiveFactor=1;
     return 1;
   }
  
   if (spann > Limit2) {
     inSensitiveFactor=1;
     return 1;
   }

   return 0; // No touch
}

volatile unsigned long pulseStart11=0;
volatile unsigned long pulseStop11=0;
volatile int pulser=0;
// Tre kommandon
// 1. En puls längre än 0.2 sekunder  => setBaseValue alltså kalibrering av nollvärde
// 2. N pulser kortare 0.1 sekunder =>  setBaseValueLimit = 1-N
// 3. 100 (exakt) pulser kortare än 10 msek => setBaseValue

#define stopTimer    {TIFR1 = 0; TCNT1 = 0; TCCR1B &= ~(1 << CS10);TCCR1B &= ~(1 << CS11);TCCR1B &= ~(1 << CS12); }
#define startTimer   {TCNT1  = 0;  TIFR1  = 0;TCCR1B |= (1 << CS12);sei(); }

// Startar ca 0.2 sekunder utan aktivitet på kommandopinnen
// Ta hand om insamlad data om pulser osv
ISR(TIMER1_COMPA_vect)
{
#ifdef DEBUGUTSKRIFT
  Serial.print(" Pulser = ");
  Serial.println(pulser);
#endif

  stopTimer;
  if (pulser == 1 || pulser == 100){
    pulser = 0;
    setBaseValue();
    return; 
  }
  if (pulser == 2){
    pulser = 0;
    inSensitiveFactor=3;
    return; 
  }
  if (pulser == 3 || pulser == 101){
    pulser = 0;
    blinkPulser=baseValuePulser; 
    return; 
  }
  if (pulser > 1) {
      baseValuePulser = pulser;
      saveEEPROM=true;
  }
  pulser = 0;
}

// ISR för input på kommandopinnen pinne-11
void pin11()
{
  if (digitalRead(PIN11)) {  // UP
    pulseStart11 = micros();
  } else {  // DOWN - completing the pulse
    pulseStop11 = micros();
 
    if (pulseStop11 - pulseStart11 < 10000) { // Kommando 2,3. Pulser kortare än 0.1 sekund
      pulser++;
      startTimer;             // Reload
    } else {                  // Fail. reset
      pulseStop11 = pulseStart11 = 0; // Reset
      pulser = 0;
      pin11Fail = true;
    }
  }    
}

void setup() 
{
    for (int i=0; i < N_ADC; i++) {
        pinMode(A0+i, INPUT);      // For ADC
        digitalWrite(A0+i, HIGH);  // Enable internal pullups
     }
    setBaseValue();                // Initial
    baseValuePulser = EEPROM.read(EEPROM_POS);  // Orka ställa in varje gång. Nä...
    if (baseValuePulser < STARTING_LIMIT/2 || baseValuePulser > 100) {
      baseValuePulser = STARTING_LIMIT;
      EEPROM.write(EEPROM_POS, baseValuePulser); // För första gången är tanken. Korrigerar kasst värde
    } 
    
    pinMode(PIN11, INPUT);         // Kommandopinnen
    digitalWrite(PIN11, HIGH);
    enableInterrupt(11, &pin11, CHANGE); // M40 S0

    cli();                         // Initiera timern
    TCCR1A = 0;
    TCCR1B = 0;
    TCNT1  = 0;
    TIMSK1  |= (1 << OCIE1A);      
    OCR1A = 625;                   // Set compare match register for 100 Hz increments
    TCCR1B  |= (1 << WGM12);       // Turn on CTC mode
    sei();
  
    pinMode(12, OUTPUT);           // Output to RAMPS
    digitalWrite(12, HIGH);        // Off
    
    pinMode(13, OUTPUT);           // LED pin
    digitalWrite(13, LOW);         // Inverted output to RAMPS

    blinkPulser=EEPROM.read(EEPROM_POS);
    saveEEPROM=false;

#ifdef DEBUGUTSKRIFT
    Serial.begin(115200);
#endif
}

void loop() 
{
    if (touching()) {
        digitalWrite(12, LOW);
        digitalWrite(13, HIGH);
    } else {
        digitalWrite(12, HIGH);      
        digitalWrite(13, LOW);
    }
    if (blinkPulser > 0) {
      digitalWrite(13, LOW);
      delay(1000);
      for (; blinkPulser >= 10; blinkPulser = blinkPulser - 10) {
        digitalWrite(13, HIGH);
        delay(1000);
        digitalWrite(13, LOW);
        delay(1000);
      }
      while(blinkPulser-- > 0) {
        digitalWrite(13, HIGH);
        delay(300);
        digitalWrite(13, LOW);
        delay(300);
      }
      delay(300);
      blinkPulser=0;
    }
    if (saveEEPROM) {
      EEPROM.write(EEPROM_POS, baseValuePulser); // Spara tills nästa gång
      for (int  i=0; i < 3; i++) {
        digitalWrite(13,HIGH);
        delay(50);
        digitalWrite(13,LOW);
        delay(50);
      }
      saveEEPROM=false;
    }
    if (pin11Fail) {
      for (int  i=0; i < 10; i++) {
        digitalWrite(13,HIGH);
        delay(50);
        digitalWrite(13,LOW);
        delay(50);
      }
      pin11Fail = false;
    }
}
