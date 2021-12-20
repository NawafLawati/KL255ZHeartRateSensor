#include "mbed.h"
#include "fix_fft.h"

DigitalOut myled(LED1);


AnalogIn analogueIn(PTB0);
AnalogOut diagnostic_probe(PTE30);

//Used to switch between numerical or graphical display mode.
// WE HAVE CHANGED IT FROM PTD1 TO PTD4 AS IT IS THE CORRECT PIN
DigitalIn mode_switch(PTD4);

DigitalOut chip_select(PTD0);

SPI max72_spi(PTD2, PTD3, PTD1);

#define register_noop        0x00
#define register_digit0       0x01
#define register_digit1       0x02
#define register_digit2       0x03
#define register_digit3       0x04
#define register_digit4       0x05
#define register_digit5       0x06
#define register_digit6       0x07
#define register_digit7       0x08
#define register_decodeMode   0x09
#define register_intensity    0x0a
#define register_scanLimit    0x0b
#define register_shutdown     0x0c
#define register_displayTest  0x0f

//Patterns of digits zero through 9
const char pattern_digit[10][4] =  {
    {0x7C, 0x82, 0x82, 0x7C},
    {0x00, 0x42, 0xFE, 0x02},
    {0x46, 0x8A, 0x92, 0x62},
    {0x44, 0x82, 0x92, 0x6C},
    {0x18, 0x28, 0x48, 0xFE},
    {0xE4, 0xA2, 0xA2, 0x9C},
    {0x7C, 0x92, 0x92, 0x4C},
    {0x80, 0x80, 0x80, 0xFE},
    {0x6C, 0x92, 0x92, 0x6C},
    {0x64, 0x92, 0x92, 0x7C}
};

const char pattern_clear[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
const char pattern_error[8] = {0xF8, 0xA8, 0x88, 0x38, 0x20, 0x00, 0x38, 0x20};
// ADDED FOR WHEN OVER 100 BPM
const char pattern_100[2] = {0xF8, 0x00};
const char pattern_digit_1xx[10][3] = {
    {0xf8, 0x88, 0xf8},
    {0x00, 0xF8, 0x00},
    {0xb8, 0xa8, 0xe8},
    {0x88, 0xa8, 0xf8},
    {0xe0, 0x20, 0xf8},
    {0xe8, 0xa8, 0xb8},
    {0xF8, 0xa8, 0xb8},
    {0x80, 0x80, 0xf8},
    {0xf8, 0xa8, 0xf8},
    {0xe0, 0xa0, 0xf8}
};

int16_t fft_real[1024];
int16_t fft_imag[1024];
int fft_idx = 0;

//Array of raw sample data
uint16_t samples_raw[512];
uint16_t samples_norm[512];

uint16_t trend_ewma = 0;

uint16_t value_out = 0;
char display_out[8];

int display_wait = 10;

int bpm = -1;

//The index at which processing should start counting to process 100 samples
int processing_index=0;

//The index of the current sample, at 100 will allow processing
uint16_t sample_index=0;

//Store for processed samples
double samples_processed[200];

Ticker tick;

const float sample_rate = 0.0125f;
const float display_rate = 0.125f;

void sample(void);
void display(void);
void sample_signal(void);
void process_sample(void);
void process_display_data(void);
void update_display(void);
void write_pattern(const char*,int,int);
void write_to_display(int,int);

int temp_counter=0;

int main()
{
    //SPI setup
    max72_spi.format(8, 0);
    max72_spi.frequency(100000);

    //Display setup
    max72_spi.write(register_scanLimit);
    max72_spi.write(0x07);
    max72_spi.write(register_decodeMode);
    max72_spi.write(0x00);
    max72_spi.write(register_shutdown);
    max72_spi.write(0x01);
    max72_spi.write(register_displayTest);
    max72_spi.write(0x00);
    for (int e=1; e<=8; e++) {
        max72_spi.write(e);
        max72_spi.write(pattern_error[e]);
    }
    // maxAll(max7219_reg_intensity, 0x0f & 0x0f);    // the first 0x0f is the value you can set
    max72_spi.write(register_intensity);
    max72_spi.write(0x08);

    //Setup the tickers
    tick.attach(&sample, sample_rate);
    //tick.attach(&display, display_rate);

    myled=1;
    bpm = 0;
    while(1) {

        //    bpm++;
        //   if(bpm > 200) bpm = 0;
        wait(0.5);
    }
}



void sample()
{
    sample_signal();
    process_sample();
    if(!--display_wait) {
        display_wait = 10;
        display();
    }
}

void display()
{
    myled=int(mode_switch);
    process_display_data();
    update_display();
}

void sample_signal()
{
    temp_counter++;
    //Store samples
    sample_index++;
    sample_index &= 0x1ff;
    samples_raw[sample_index]=analogueIn.read_u16();
}

extern const short Sinewave[768];

//Processes the signal
void process_sample()
{
    trend_ewma += ((samples_raw[sample_index] >> 5) - (trend_ewma >> 5));
    samples_norm[sample_index] = samples_raw[sample_index] + (32788 - trend_ewma);

/*
    fft_real[fft_idx] = ((int32_t)samples_raw[sample_index]) - 0x10000;
    fft_imag[fft_idx++] = 0;
    if(fft_idx == 1024) {
        int max_idx = 0;
        int max_sqmag = 0;
        fix_fft(fft_real, fft_imag, 10, 0);

        for(int i = 8; i < 44; i++) {
            int sq_magnitude = fft_real[i] * fft_real[i] + fft_imag[i] * fft_imag[i];
            printf("%i : %i\r\n", i, sq_magnitude);
            if(sq_magnitude > max_sqmag) {
                max_idx = i;
                max_sqmag = sq_magnitude;
            }
        }

        bpm = int(4.6875f * max_idx);
        printf("Idx : %i ; BPM : %i\r\n", max_idx, bpm);
        fft_idx = 0;

    }
*/
    printf("%i\r\n", samples_norm[sample_index]);
    diagnostic_probe.write_u16(samples_norm[sample_index]);
}

//Takes the processed signal data and either;
// turns it into patterns for the display for the number mode
// or turns it into graphical data
void process_display_data()
{
    /*  uint16_t i;
      value_out = 0;
      for(i = 0; i < 16; i++) {
          value_out += samples_raw[(sample_index-i)&0x1ff] >> 4;
      }*/
    value_out = samples_norm[sample_index];
}


//Takes display data in the form of graphical or pattern data and sends it to the display
void update_display()
{
    if (mode_switch==0) {
        //numeric
        if(bpm > 199 || bpm < 0) { /* Error */
            write_pattern(pattern_error, 0, 8);
        } else if(bpm > 99) {
            int digs = bpm - 100;
            write_pattern(pattern_100, 0, 2);
            write_pattern(pattern_digit_1xx[digs/10], 2, 3);
            write_pattern(pattern_digit_1xx[digs%10], 5, 3);
        } else {
            write_pattern(pattern_digit[bpm/10], 0, 4);
            write_pattern(pattern_digit[bpm%10], 4, 4);
        }
    } else {
        //graphical
        display_out[0] = display_out[1];
        display_out[1] = display_out[2];
        display_out[2] = display_out[3];
        display_out[3] = display_out[4];
        display_out[4] = display_out[5];
        display_out[5] = display_out[6];
        display_out[6] = display_out[7];
        display_out[7] = 1 << (value_out >> 13);
        write_pattern(display_out, 0, 8);
    }
}

//Takes a pattern and uses write_to_display to send that pattern to the display
void write_pattern(const char *pattern_data, int pos, int len)
{
    int char_data;
    for (int count=0; count<len; count++) {
        char_data = pattern_data[count];
        write_to_display(register_digit7 - pos - count, char_data);
    }
}

//Sends a column of data to the display
void write_to_display(int reg, int column)
{
    chip_select = 0;
    max72_spi.write(reg);
    max72_spi.write(column);
    chip_select = 1;
}
