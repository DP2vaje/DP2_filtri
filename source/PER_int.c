/****************************************************************
* FILENAME:     PER_int.c
* DESCRIPTION:  periodic interrupt code
* AUTHOR:       Mitja Nemec
* DATE:         16.1.2009
*
****************************************************************/
#include    "PER_int.h"
#include    "TIC_toc.h"

// za izracunun napetostni
long napetost_raw = 0;
float napetost_offset = 1971;
float napetost_gain = ???;
float napetost = 0.0;

// za izracun toka
long tok_raw = 0;
float tok_offset = 1940;
float tok_gain = ???;
float tok = 0.0;

// za kalibracijo preostale napetosti tokovne sonde
bool    offset_calib = TRUE;
long     offset_counter = 0;
float   offset_filter = 0.05;

// vklopno razmerje
float duty = 0.0;

// generiranje testnega signala
float   f_osn = 100;
float   amp_osn = 0.5;
float   kot_osn = 0.0;
float   f_harm = 1000;
float   amp_harm = 0.1;
float   kot_harm = 0.0;
float   test_out = 0.0;
float	dc_offset = 0.1;

// za oceno obremenjenosti CPU-ja
float   cpu_load  = 0.0;
long    interrupt_cycles = 0;


// spremenljikva s katero štejemo kolikokrat se je prekinitev predolgo izvajala
int interrupt_overflow_counter = 0;

// format zapisa
#define FORMAT  12  //4.12

// spremenljivke, ki jih potrebujemo



/**************************************************************
* Prekinitev, ki v kateri se izvaja regulacija
**************************************************************/
#pragma CODE_SECTION(PER_int, "ramfuncs");
void interrupt PER_int(void)
{
    /* lokalne spremenljivke */
    
    // najprej povem da sem se odzzval na prekinitev
    // Spustimo INT zastavico casovnika ePWM1
    EPwm1Regs.ETCLR.bit.INT = 1;
    // Spustimo INT zastavico v PIE enoti
    PieCtrlRegs.PIEACK.all = PIEACK_GROUP3;
    
    // pozenem stoprico
    interrupt_cycles = TIC_time;
    TIC_start();

    // izracunam obremenjenost procesorja
    cpu_load = (float)interrupt_cycles / (CPU_FREQ/SWITCH_FREQ);

    // pocakam da ADC konca s pretvorbo
    ADC_wait();

    // ali kalibriram preostalo napetost tokovne sonde
    if (offset_calib == TRUE)
    {
        PWM_update(0.0);
        tok_raw = TOK;
        tok_offset = (1.0 - offset_filter) * tok_offset + offset_filter * tok_raw;

        napetost_raw = NAPETOST;
        napetost_offset = (1.0 - offset_filter) * napetost_offset + offset_filter * napetost_raw;

        offset_counter = offset_counter + 1;
        if ( offset_counter == 5L * SWITCH_FREQ)
        {
            offset_calib = FALSE;
        }

    }
    // sicer pa normalno obratujem
    else
    {
        // preracunam napetost
        napetost_raw = NAPETOST - napetost_offset;
        napetost = napetost_raw * napetost_gain;

        tok_raw = TOK - tok_offset;
        tok = -tok_raw * tok_gain;
        
        /*
        * tukaj pride koda filtrov
        */
        
        
        
        

        // generiram testni signal
        kot_osn = kot_osn + f_osn * SAMPLE_TIME;
        if (kot_osn >= 1.0)
        {
            kot_osn = kot_osn - 1.0;
        }
        kot_harm = kot_harm + f_harm * SAMPLE_TIME;
        if (kot_harm >= 1.0)
        {
            kot_harm = kot_harm - 1.0;
        }

        test_out = dc_offset
        		 + amp_osn * sin(2*PI*kot_osn)
                 + amp_harm * sin(2*PI*kot_harm);

        // posljem testni signal na izhod
        duty = test_out;

        // osvežim vklono razmerje
        PWM_update(duty);

        // spavim vrednosti v buffer za prikaz
        DLOG_GEN_update();

    }


    
    /* preverim, èe me sluèajno èaka nova prekinitev.
       èe je temu tako, potem je nekaj hudo narobe
       saj je èas izvajanja prekinitve predolg
       vse skupaj se mora zgoditi najmanj 10krat,
       da reèemo da je to res problem
    */
    if (EPwm1Regs.ETFLG.bit.INT == TRUE)
    {
        // povecam stevec, ki steje take dogodke
        interrupt_overflow_counter = interrupt_overflow_counter + 1;
        
        // in ce se je vse skupaj zgodilo 10 krat se ustavim
        // v kolikor uC krmili kakšen resen HW, potem moèno
        // proporoèam lepše "hendlanje" takega dogodka
        // beri:ugasni moènostno stopnjo, ...
        if (interrupt_overflow_counter >= 10)
        {
            asm(" ESTOP0");
        }
    }
    
    // stopam
    TIC_stop();

}   // end of PWM_int

/**************************************************************
* Funckija, ki pripravi vse potrebno za izvajanje
* prekinitvene rutine
**************************************************************/
void PER_int_setup(void)
{

    // Proženje prekinitve 
    EPwm1Regs.ETSEL.bit.INTSEL = ET_CTR_ZERO;    //sproži prekinitev na periodo
    EPwm1Regs.ETPS.bit.INTPRD = ET_1ST;         //ob vsakem prvem dogodku
    EPwm1Regs.ETCLR.bit.INT = 1;                //clear possible flag
    EPwm1Regs.ETSEL.bit.INTEN = 1;              //enable interrupt

    // inicializiram data logger

    dlog.slope = Positive;                 // trigger on positive slope
    dlog.prescalar = 1;                    // store every  sample
    dlog.mode = Normal;                    // Normal trigger mode
    dlog.auto_time = 100;                  // number of calls to update function
    dlog.holdoff_time = 100;               // number of calls to update function

    dlog.trig = &kot_osn;
    dlog.trig_value = 0.5;                  // specify trigger value

    dlog.iptr1 = &napetost;
    dlog.iptr2 = &tok;

    // registriram prekinitveno rutino
    EALLOW;
    PieVectTable.EPWM1_INT = &PER_int;
    EDIS;
    PieCtrlRegs.PIEACK.all = PIEACK_GROUP3;
    PieCtrlRegs.PIEIER3.bit.INTx1 = 1;
    IER |= M_INT3;
    // da mi prekinitev teèe  tudi v real time naèinu
    // (za razhoršèevanje main zanke in BACK_loop zanke)
    SetDBGIER(M_INT3);
}
