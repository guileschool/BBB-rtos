/* Scheduler includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "am335.h"
#include "serial.h"
#include "SevenSegmentDisplay.h"

typedef enum { false, true } bool;

static int numberSegments;   //!< The number of segments connected
static int numberBase;       //!< Base can be between 2 (binary) and 16 (hexadecimal). Default is decimal (base 10)
static bool isCommonAnode;   //!< Is a common anode display -- false by default

enum{
  LED_A,
  LED_B,
  LED_C,
  LED_D,
  LED_E,
  LED_F,
  LED_G,
  LED_H,
  MAX_LEDS
};

#define DIG1  0b00000001
#define DIG2  0b00000010
#define DIG3  0b00000100
#define DIG4  0b00001000

static void digitalWrite(int seg, bool sel);
static void select_SegDigit(int sel, bool others);

/*
GPIO2_2(7)    --> 1(e)TIMER4        GPIO0_27(17)--> 12(DIG1)GPIO0_27
GPIO2_5(9)    --> 2(d)TIMER5        GPIO1_12(12)--> 11(a)GPIO1_12
GPIO1_15(15)  --> 3(h)GPIO1_15      GPIO1_29(10)--> 10(f)GPIO1_29
GPIO0_23(13)  --> 4(c)EHRPWM2B      GPIO0_22(19)--> 9(DIG2)EHRPWM2A
GPIO2_3(8)    --> 5(g)TIMER7        GPIO2_1(18) --> 8(DIG3)GPIO2_1
GPIO1_13(11)  --> 6(DIG4)GPIO1_13   GPIO1_14(16)--> 7(b)GPIO1_14

890h GPMC_ADVN_ALE/TIMER4/GPIO2_2(7) --> 1(e)      
89Ch GPMC_BE0N_CLE/TIMER5/GPIO2_5(9) --> 2(d)
83ch GPMC_AD15/LCD_DATA16/MMC1_DAT7/MMC2_DAT3/EQEP2_STROBE/PR1_ECAP0_ECAP_CAPIN_APWM_O/PR1_PRU0_PRU_R31_15/GPIO1_15(15) --> 3(h)
824h GPMC_AD9/LCD_DATA22/MMC1_DAT1/MMC2_DAT5/EHRPWM2B/PR1_MII0_CRS//GPIO0_23(13) --> 4(c)      
894h GPMC_OEN_REN/TIMER7/EMU4/GPIO2_3(8) --> 5(g)    
834h GPMC_AD13/LCD_DATA18/MMC1_DAT5/MMC2_DAT1/EQEP2B_IN/PR1_MII0_TXD1/PR1_PRU0_PRU_R30_15/GPIO1_13(11) --> 6(DIG4)
838h GPMC_AD14/LCD_DATA17/MMC1_DAT6/MMC2_DAT2/EQEP2_INDEX/PR1_MII0_TXD0/PR1_PRU0_PRU_R31_14/GPIO1_14(16) --> 7(b)
88Ch GPMC_CLK/LCD_MEM_CLK/GPMC_WAIT1/MMC2_CLK/PRT1_MII1_TXEN/MCASP0_FSR/GPIO2_1(18) --> 8(DIG3)
820h GPMC_AD8/LCD_DATA23/MMC1_DAT0/MMC2_DAT4/EHRPWM2A/PR1_MII_MT0_CLK//GPIO0_22(19) --> 9(DIG2)
87Ch GPMC_CSN0/GPIO1_29(10) --> 10(f) 
830h GPMC_AD12/LCD_DATA19/MMC1_DAT4/MMC2_DAT0/EQEP2A_IN/PR1_MII0_TXD2/PR1_PRU0_PRU_R30_14/GPIO1_12(12) --> 11(a)
82Ch GPMC_AD11/LCD_DATA20/MMC1_DAT3/MMC2_DAT7/EHRPWM2_SYNCI_O/PR1_MII0_TXD3//GPIO0_27(17) --> 12(DIG1)
*/

// The binary data that describes the LED state for each symbol
// A(top) B(top right) C(bottom right) D(bottom)
// E(bottom left) F(top left) G(middle)  H(dot)
static const unsigned char symbols_4d[16] = { //(msb) HGFEDCBA 3/5/10/1/2/4/7/11 (lsb)
     0b00111111, 0b00000110, 0b01011011, 0b01001111,     //0123
     0b01100110, 0b01101101, 0b01111101, 0b00000111,     //4567
     0b01111111, 0b01100111, 0b01110111, 0b01111100,     //89Ab
     0b00111001, 0b01011110, 0b01111001, 0b01110001      //CdEF
};

/**
 * The constructor for the 7-segment display that defines the number of segments.
 * @param device The pointer to the SPI device bus
 * @param numberSegments The number of 7-segment modules attached to the bus
 */
void SevenSegment_Init(int n) {
  numberSegments = n;
  numberBase = 10; //decimal by default
  isCommonAnode = false;
}

int SevenSegment_setNumberBase(int base){
  if (base>16 || base<2) return -1;
  return (numberBase = base);
}

static int seg_disp(int number){
  int i=0;

  // going to send one character for each segment
  unsigned char output[numberSegments];

  output[i] = symbols_4d[number%numberBase];
  if(isCommonAnode) output[i]=~output[i]; //invert the bits for common anode

    if(output[i] & (1<<LED_A)) (*(REG32(GPIO0_BASE + GPIO_SETDATAOUT))) = PIN26; // 'a'
    else  (*(REG32(GPIO0_BASE + GPIO_CLEARDATAOUT))) = PIN26; // 'a'
    if(output[i] & (1<<LED_B)) (*(REG32(GPIO1_BASE + GPIO_SETDATAOUT))) = PIN14; // 'b'
    else  (*(REG32(GPIO1_BASE + GPIO_CLEARDATAOUT))) = PIN14; // 'b'
    if(output[i] & (1<<LED_C)) (*(REG32(GPIO0_BASE + GPIO_SETDATAOUT))) = PIN23; // 'c'
    else  (*(REG32(GPIO0_BASE + GPIO_CLEARDATAOUT))) = PIN23; // 'c'
    if(output[i] & (1<<LED_D)) (*(REG32(GPIO2_BASE + GPIO_SETDATAOUT))) = PIN5; // 'd'
    else  (*(REG32(GPIO2_BASE + GPIO_CLEARDATAOUT))) = PIN5; // 'd'
    if(output[i] & (1<<LED_E)) (*(REG32(GPIO2_BASE + GPIO_SETDATAOUT))) = PIN2; // 'e'
    else  (*(REG32(GPIO2_BASE + GPIO_CLEARDATAOUT))) = PIN2; // 'e'
    if(output[i] & (1<<LED_F)) (*(REG32(GPIO1_BASE + GPIO_SETDATAOUT))) = PIN29; // 'f'
    else  (*(REG32(GPIO1_BASE + GPIO_CLEARDATAOUT))) = PIN29; // 'f'
    if(output[i] & (1<<LED_G)) (*(REG32(GPIO2_BASE + GPIO_SETDATAOUT))) = PIN3; // 'g'
    else  (*(REG32(GPIO2_BASE + GPIO_CLEARDATAOUT))) = PIN3; // 'g'
    if(output[i] & (1<<LED_H)) (*(REG32(GPIO1_BASE + GPIO_SETDATAOUT))) = PIN15; // 'h'
    else  (*(REG32(GPIO1_BASE + GPIO_CLEARDATAOUT))) = PIN15; // 'h'

  return 0;
}

int SevenSegment_display1(int number){
  int intNumber = (int) number;

  digitalWrite(DIG1, false); // first digit is off
  digitalWrite(DIG2, false);
  digitalWrite(DIG3, false);
  seg_disp(intNumber); 
  digitalWrite(DIG4, true);

  return 0;
}

int SevenSegment_display2(int number){
  uint32_t xLastWakeTime;
  // can display non-decimal floats
  int intNumber = (int) number;
  const uint32_t xFrequency = 1;

  // Initialise the xLastWakeTime variable with the current time.
  xLastWakeTime = xTaskGetTickCount();

  digitalWrite(DIG3, false); // 3rd digit is off
  digitalWrite(DIG4, false); // 4th digit is off

//  select_SegDigit(DIG1, true);
  seg_disp(intNumber/numberBase);
  digitalWrite(DIG1, true); // first digit is on
  vTaskDelayUntil(&xLastWakeTime, xFrequency);

  intNumber = intNumber%numberBase;  // remainder of 1234/1000 is 234
  digitalWrite(DIG1, false); // first digit is off
  seg_disp(intNumber); //// segments are set to display "2"
  digitalWrite(DIG2, true); // second digit is on
  vTaskDelayUntil(&xLastWakeTime, xFrequency);

  return 0;
}

int SevenSegment_display4(int number){
  uint32_t xLastWakeTime;
  // can display non-decimal floats
  int intNumber = (int) number;
  const uint32_t xFrequency = 1;

  // Initialise the xLastWakeTime variable with the current time.
  xLastWakeTime = xTaskGetTickCount();

  select_SegDigit(DIG1, true);
  seg_disp(intNumber/(numberBase*numberBase*numberBase));
  digitalWrite(DIG1, true); // first digit is off
  vTaskDelayUntil(&xLastWakeTime, xFrequency);

  intNumber = intNumber%(numberBase*numberBase*numberBase);  // remainder of 1234/1000 is 234
  digitalWrite(DIG1, false); // first digit is off
  seg_disp(intNumber/(numberBase*numberBase)); //// segments are set to display "2"
  digitalWrite(DIG2, true); // second digit is on
  vTaskDelayUntil(&xLastWakeTime, xFrequency);

  intNumber =intNumber%(numberBase*numberBase);    
  digitalWrite(DIG2, false);
  seg_disp(intNumber/numberBase);
  digitalWrite(DIG3, true);
  vTaskDelayUntil(&xLastWakeTime, xFrequency);

  intNumber =intNumber%numberBase; 
  digitalWrite(DIG3, false);
  seg_disp(intNumber); 
  digitalWrite(DIG4, true);
  vTaskDelayUntil(&xLastWakeTime, xFrequency);

  return 0;
}

static void digitalWrite(int seg, bool sel)
{
  switch(seg)
  {
    case DIG1:
    if(sel) (*(REG32(GPIO0_BASE + GPIO_CLEARDATAOUT))) = PIN27; // 'SEL'
    else (*(REG32(GPIO0_BASE + GPIO_SETDATAOUT))) = PIN27; // 'NO SEL'
    break;
    case DIG2:
    if(sel) (*(REG32(GPIO0_BASE + GPIO_CLEARDATAOUT))) = PIN22; // 'SEL'
    else (*(REG32(GPIO0_BASE + GPIO_SETDATAOUT))) = PIN22; // 'NO SEL'
    break;
    case DIG3:
    if(sel) (*(REG32(GPIO2_BASE + GPIO_CLEARDATAOUT))) = PIN1; // 'SEL'
    else (*(REG32(GPIO2_BASE + GPIO_SETDATAOUT))) = PIN1; // 'NO SEL'
    break;
    case DIG4:
    if(sel) (*(REG32(GPIO1_BASE + GPIO_CLEARDATAOUT))) = PIN13; // 'SEL'
    else (*(REG32(GPIO1_BASE + GPIO_SETDATAOUT))) = PIN13; // 'NO SEL'
    break;

  }
}

static void select_SegDigit(int sel, bool others)
{
// GND output
    if(sel & DIG1) (*(REG32(GPIO0_BASE + GPIO_CLEARDATAOUT))) = PIN27; // 'SEL'
    if(others) (*(REG32(GPIO0_BASE + GPIO_SETDATAOUT))) = PIN27; // 'NO SEL'
    if(sel & DIG2) (*(REG32(GPIO0_BASE + GPIO_CLEARDATAOUT))) = PIN22; // 'SEL'
    if(others) (*(REG32(GPIO0_BASE + GPIO_SETDATAOUT))) = PIN22; // 'NO SEL'
    if(sel & DIG3) (*(REG32(GPIO2_BASE + GPIO_CLEARDATAOUT))) = PIN1; // 'SEL'
    if(others) (*(REG32(GPIO2_BASE + GPIO_SETDATAOUT))) = PIN1; // 'NO SEL'
    if(sel & DIG4) (*(REG32(GPIO1_BASE + GPIO_CLEARDATAOUT))) = PIN13; // 'SEL'
    if(others) (*(REG32(GPIO1_BASE + GPIO_SETDATAOUT))) = PIN13; // 'NO SEL'
}
