#include <mega32.h>
#include <lcd.h>
#include <delay.h>
#include <stdio.h>
#include <stdlib.h>

/*
   CodeVisionAVR LCD default 4-bit connection on PORTC:
   RS = PC0
   RW = PC1
   E  = PC2
   D4 = PC4
   D5 = PC5
   D6 = PC6
   D7 = PC7
*/
#asm
   .equ __lcd_port=0x15
#endasm

/* -------------------- ADC channels -------------------- */
#define ADC_TEMP_CH      0   // PA0 / ADC0 -> LM35
#define ADC_SETPOINT_CH  2   // PA2 / ADC2 -> Potentiometer
#define ADC_SOIL_CH      5   // PA5 / ADC5 -> Soil sensor
#define ADC_LIGHT_CH     6   // PA6 / ADC6 -> LDR divider

/* -------------------- Inputs -------------------- */
/* PA1: Auto/Manual select (pull-up enabled)
   PA3: Manual light switch (pull-up enabled)
   PA4: Pump enable switch (pull-up enabled) */

#define AUTO_MODE_INPUT      PINA.1
#define MANUAL_LIGHT_INPUT   PINA.3
#define PUMP_ENABLE_INPUT    PINA.4

/* -------------------- Outputs on PORTB -------------------- */
#define GROW_LIGHT_1   PORTB.6
#define GROW_LIGHT_2   PORTB.7
#define COOLING_FAN    PORTB.4
#define HEATER_OUT     PORTB.5

#define PUMP_CTRL_0    PORTB.0
#define PUMP_CTRL_1    PORTB.1
#define PUMP_CTRL_2    PORTB.2
/* PB3 / OC0 -> PWM output to pump driver */

/* -------------------- Tuning / calibration -------------------- */
#define ADC_REF_VOLTAGE      5.0

/* Soil sensor calibration:
   adjust these two values using your real sensor */
#define SOIL_DRY_ADC         850
#define SOIL_WET_ADC         350

/* Moisture thresholds (%) */
#define SOIL_PUMP_HIGH_ON    20
#define SOIL_PUMP_LOW_ON     35

/* Light thresholds (Volts) */
#define LIGHT_LOW_V          1.50
#define LIGHT_MID_V          2.50

/* Temperature control */
#define TEMP_SET_MIN_C       18.0
#define TEMP_SET_MAX_C       35.0
#define TEMP_HYSTERESIS_C    1.5

char buf1[10];
char buf2[10];
char buf3[10];
char buf4[6];

/* -------------------- Utility -------------------- */
int clamp_int(int value, int min_v, int max_v)
{
    if (value < min_v) return min_v;
    if (value > max_v) return max_v;
    return value;
}

/* -------------------- ADC -------------------- */
void adc_init(void)
{
    /* AVCC reference, right adjusted, ADC enabled, prescaler = 64 */
    ADMUX  = 0x40;
    ADCSRA = 0x86;
}

unsigned int adc_read(unsigned char channel)
{
    ADMUX = 0x40 | (channel & 0x07);   // AVCC ref + channel
    delay_us(20);

    ADCSRA |= 0x40;                    // Start conversion
    while ((ADCSRA & 0x10) == 0);      // Wait for completion
    ADCSRA |= 0x10;                    // Clear ADIF

    return ADCW;
}

/* -------------------- PWM on Timer0 / OC0 (PB3) -------------------- */
void pwm0_init(void)
{
    /* Fast PWM, non-inverting, prescaler = 64 */
    TCCR0 = 0x6B;
    OCR0  = 0;
}

/* -------------------- I/O -------------------- */
void io_init(void)
{
    DDRA = 0x00;   // Sensors + switches
    DDRB = 0xFF;   // Outputs
    DDRC = 0xFF;   // LCD
    DDRD = 0xFF;

    /* Enable pull-ups only for the switches */
    PORTA.1 = 1;
    PORTA.3 = 1;
    PORTA.4 = 1;

    PORTB = 0x00;
    PORTD = 0x00;
}

/* -------------------- Conversion helpers -------------------- */
float adc_to_voltage(unsigned int adc_value)
{
    return ((float)adc_value * ADC_REF_VOLTAGE) / 1023.0;
}

float adc_to_temperature_c(unsigned int adc_value)
{
    /* LM35 = 10 mV / °C */
    return adc_to_voltage(adc_value) * 100.0;
}

float adc_to_setpoint_c(unsigned int adc_value)
{
    return TEMP_SET_MIN_C +
           (((float)adc_value) * (TEMP_SET_MAX_C - TEMP_SET_MIN_C) / 1023.0);
}

int adc_to_soil_percent(unsigned int adc_value)
{
    long percent;

    /* 0% = dry, 100% = wet */
    percent = ((long)SOIL_DRY_ADC - (long)adc_value) * 100L /
              ((long)SOIL_DRY_ADC - (long)SOIL_WET_ADC);

    return clamp_int((int)percent, 0, 100);
}

/* -------------------- Control functions -------------------- */
void control_lighting(float light_voltage, unsigned char auto_mode, unsigned char manual_light_on)
{
    if (auto_mode)
    {
        if (light_voltage <= LIGHT_LOW_V)
        {
            GROW_LIGHT_1 = 1;
            GROW_LIGHT_2 = 1;
        }
        else if (light_voltage < LIGHT_MID_V)
        {
            GROW_LIGHT_1 = 1;
            GROW_LIGHT_2 = 0;
        }
        else
        {
            GROW_LIGHT_1 = 0;
            GROW_LIGHT_2 = 0;
        }
    }
    else
    {
        if (manual_light_on)
        {
            GROW_LIGHT_1 = 1;
            GROW_LIGHT_2 = 1;
        }
        else
        {
            GROW_LIGHT_1 = 0;
            GROW_LIGHT_2 = 0;
        }
    }
}

void control_temperature(float temp_c, float setpoint_c)
{
    if (temp_c > (setpoint_c + TEMP_HYSTERESIS_C))
    {
        COOLING_FAN = 1;
        HEATER_OUT  = 0;
    }
    else if (temp_c < (setpoint_c - TEMP_HYSTERESIS_C))
    {
        COOLING_FAN = 0;
        HEATER_OUT  = 1;
    }
    else
    {
        COOLING_FAN = 0;
        HEATER_OUT  = 0;
    }
}

void control_pump(int soil_percent, unsigned char pump_enable)
{
    if (!pump_enable)
    {
        OCR0 = 0;
        PUMP_CTRL_0 = 0;
        PUMP_CTRL_1 = 0;
        PUMP_CTRL_2 = 0;
        return;
    }

    /* Enable pump driver block */
    PUMP_CTRL_0 = 1;
    PUMP_CTRL_1 = 1;
    PUMP_CTRL_2 = 0;

    if (soil_percent <= SOIL_PUMP_HIGH_ON)
    {
        OCR0 = 220;    // strong watering
    }
    else if (soil_percent <= SOIL_PUMP_LOW_ON)
    {
        OCR0 = 140;    // light watering
    }
    else
    {
        OCR0 = 0;      // soil wet enough
    }
}

/* -------------------- LCD -------------------- */
void lcd_show_startup(void)
{
    lcd_clear();
    lcd_gotoxy(0,0);
    lcd_putsf("SMART FLOWERPOT");
    lcd_gotoxy(0,1);
    lcd_putsf("ATmega32 SYSTEM");
    delay_ms(2000);
    lcd_clear();
}

void lcd_show_data(float temp_c, float setpoint_c, float light_v, int soil_percent)
{
    lcd_gotoxy(0,0);
    lcd_putsf("                ");
    lcd_gotoxy(0,0);
    lcd_putsf("T=");
    ftoa(temp_c,1,buf1);
    lcd_puts(buf1);
    lcd_putsf(" S=");
    ftoa(setpoint_c,1,buf2);
    lcd_puts(buf2);

    lcd_gotoxy(0,1);
    lcd_putsf("                ");
    lcd_gotoxy(0,1);
    lcd_putsf("L=");
    ftoa(light_v,1,buf3);
    lcd_puts(buf3);
    lcd_putsf(" M=");
    itoa(soil_percent,buf4);
    lcd_puts(buf4);
    lcd_putchar('%');
}

/* -------------------- Main -------------------- */
void main(void)
{
    unsigned int adc_temp, adc_set, adc_soil, adc_light;
    float temp_c, setpoint_c, light_v;
    int soil_percent;

    unsigned char auto_mode;
    unsigned char manual_light_on;
    unsigned char pump_enable;

    io_init();
    adc_init();
    pwm0_init();
    lcd_init(16);
    lcd_show_startup();

    while (1)
    {
        adc_temp  = adc_read(ADC_TEMP_CH);
        adc_set   = adc_read(ADC_SETPOINT_CH);
        adc_soil  = adc_read(ADC_SOIL_CH);
        adc_light = adc_read(ADC_LIGHT_CH);

        temp_c      = adc_to_temperature_c(adc_temp);
        setpoint_c  = adc_to_setpoint_c(adc_set);
        light_v     = adc_to_voltage(adc_light);
        soil_percent = adc_to_soil_percent(adc_soil);

        /* Switch logic with internal pull-up:
           PA1 = 1 -> AUTO mode
           PA1 = 0 -> MANUAL mode

           PA3 = 0 -> manual light ON
           PA4 = 0 -> pump control enabled
        */
        auto_mode       = (AUTO_MODE_INPUT == 1);
        manual_light_on = (MANUAL_LIGHT_INPUT == 0);
        pump_enable     = (PUMP_ENABLE_INPUT == 0);

        control_lighting(light_v, auto_mode, manual_light_on);
        control_temperature(temp_c, setpoint_c);
        control_pump(soil_percent, pump_enable);
        lcd_show_data(temp_c, setpoint_c, light_v, soil_percent);

        delay_ms(300);
    }
}
