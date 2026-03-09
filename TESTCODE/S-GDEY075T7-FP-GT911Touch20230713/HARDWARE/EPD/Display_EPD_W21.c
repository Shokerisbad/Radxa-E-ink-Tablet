#include "Display_EPD_W21_spi.h"
#include "Display_EPD_W21.h"
#include <stdlib.h>

//unsigned char Old_data[48000];
void delay_xms(unsigned int xms)
{
	unsigned int i;
	while(xms--)
	{
		i=12000;
		while(i--);
	}
}

void EPD_W21_Init(void)
{
	EPD_W21_RST_0;		// Module reset
	delay_xms(10);//At least 10ms delay 
	EPD_W21_RST_1;
	delay_xms(10);//At least 10ms delay 
	
}
void EPD_Refresh(void)
{
	EPD_W21_WriteCMD(0x92);		//Enter normal mode
	//Refresh
	EPD_W21_WriteCMD(0x12);		//DISPLAY REFRESH 	
	delay_xms(1);	             //!!!The delay here is necessary, 200uS at least!!!     
	lcd_chkstatus();   
}
void EPD_Display(unsigned char *Image)
{
    unsigned int Width, Height,i,j;
    Width = (EPD_WIDTH % 8 == 0)? (EPD_WIDTH / 8 ): (EPD_WIDTH / 8 + 1);
    Height = EPD_HEIGHT;

    EPD_W21_WriteCMD(0x10);
    for (j = 0; j < Height; j++) {
        for ( i = 0; i < Width; i++) {
					  EPD_W21_WriteDATA(~Image[i + j * Width]);
        }
    }

    EPD_W21_WriteCMD(0x13);
    for ( j = 0; j < Height; j++) {
        for ( i = 0; i < Width; i++) {
            EPD_W21_WriteDATA(0x00);
        }
    }
		EPD_W21_WriteCMD(0x12);			//DISPLAY REFRESH 	
		delay_xms(1);	    //!!!The delay here is necessary, 200uS at least!!!     
		lcd_chkstatus();
}
void SetFrameMemory(
        const unsigned char* image_buffer,
        int x,
        int y,
        int image_width,
        int image_height
)
{
  int x_end;
  int y_end;
	int i,j;
  
 //  EPD_W21_Init();
  if (
          image_buffer == NULL ||
          x < 0 || image_width < 0 ||
          y < 0 || image_height < 0
  ) {
    return;
  }
  /* x point must be the multiple of 8 or the last 3 bits will be ignored */
  x &= 0xF8;
  image_width &= 0xF8;
  if (x + image_width >= EPD_WIDTH) {
    x_end = EPD_WIDTH - 1;
  } else {
    x_end = x + image_width - 1;
  }
  if (y + image_height >= EPD_HEIGHT) {
    y_end = EPD_HEIGHT - 1;
  } else {
    y_end = y + image_height - 1;
  }

	 //Write RAM  datas
    EPD_W21_WriteCMD(0x91);		//This command makes the display enter partial mode
		EPD_W21_WriteCMD(0x90);		//resolution setting
		EPD_W21_WriteDATA (x/256);
		EPD_W21_WriteDATA (x%256);   //x-start    		
		EPD_W21_WriteDATA (x_end/256);		
		EPD_W21_WriteDATA (x_end%256-1);  //x-end

		EPD_W21_WriteDATA (y/256);
		EPD_W21_WriteDATA (y%256);   //y-start    	
		EPD_W21_WriteDATA (y_end/256);		
		EPD_W21_WriteDATA (y_end%256-1);  //y-end
	
    EPD_W21_WriteCMD(0x10); //writes New data to SRAM.
		for ( j = 0; j < y_end - y + 1; j++) 
			for ( i = 0; i < (x_end - x + 1) / 8; i++) 
			{
				EPD_W21_WriteDATA(~image_buffer[i + j * (image_width / 8)]);
			}		
  
}

//UC8179
void EPD_init(void)
{	
		EPD_W21_Init();			//Electronic paper IC reset	
	  delay_xms(100);					//reset IC and select BUS 	

    EPD_W21_WriteCMD(0x00);
    EPD_W21_WriteDATA(0x1F);
		
	  EPD_W21_WriteCMD(0x04); //POWER ON
	  delay_xms(300);  
		lcd_chkstatus();
		EPD_W21_WriteCMD(0X50);			//VCOM AND DATA INTERVAL SETTING
		EPD_W21_WriteDATA(0x21);
		EPD_W21_WriteDATA(0x07);
		
		
}
void EPD_init_Fast(void)
{	
	EPD_W21_RST_0;  // Module reset   
	delay_xms(10);//At least 10ms delay 
	EPD_W21_RST_1;
	delay_xms(10); //At least 10ms delay 
  
	EPD_W21_WriteCMD(0X00);			//PANNEL SETTING
	EPD_W21_WriteDATA(0x1F);   //KW-3f   KWR-2F	BWROTP 0f	BWOTP 1f

	EPD_W21_WriteCMD(0X50);			//VCOM AND DATA INTERVAL SETTING
	EPD_W21_WriteDATA(0x10);
	EPD_W21_WriteDATA(0x07);

	EPD_W21_WriteCMD(0x04); //POWER ON
	delay_xms(100);  
	lcd_chkstatus();        //waiting for the electronic paper IC to release the idle signal

	//Enhanced display drive(Add 0x06 command)
	EPD_W21_WriteCMD(0x06);			//Booster Soft Start 
	EPD_W21_WriteDATA (0x27);
	EPD_W21_WriteDATA (0x27);   
	EPD_W21_WriteDATA (0x18);		
	EPD_W21_WriteDATA (0x17);		

	EPD_W21_WriteCMD(0xE0);
	EPD_W21_WriteDATA(0x02);
	EPD_W21_WriteCMD(0xE5);
	EPD_W21_WriteDATA(0x5A);
		
		
}
void EPD_display_init(void)
{	
	EPD_W21_RST_0;  // Module reset   
	delay_xms(10);//At least 10ms delay 
	EPD_W21_RST_1;
	delay_xms(10); //At least 10ms delay 

	EPD_W21_WriteCMD(0X00);			//PANNEL SETTING
	EPD_W21_WriteDATA(0x1F);   //KW-3f   KWR-2F	BWROTP 0f	BWOTP 1f
	
	EPD_W21_WriteCMD(0x04); //POWER ON
	delay_xms(100);  
	lcd_chkstatus();        //waiting for the electronic paper IC to release the idle signal
	
	EPD_W21_WriteCMD(0xE0);
	EPD_W21_WriteDATA(0x02);
	EPD_W21_WriteCMD(0xE5);
	EPD_W21_WriteDATA(0x6E);

}

void EPD_partial_display(int x_start,int y_start,const unsigned char *new_data,unsigned int PART_COLUMN,unsigned int PART_LINE,unsigned char mode) //partial display 
{
	unsigned int i,count;  
	unsigned int x_end,y_end;
	
	x_end=x_start+PART_LINE-1; 
	y_end=y_start+PART_COLUMN-1;
    count=PART_COLUMN*PART_LINE/8;	
	
		EPD_W21_WriteCMD(0x50);
		EPD_W21_WriteDATA(0xA9);
		EPD_W21_WriteDATA(0x07);
	
	  EPD_W21_WriteCMD(0x91);		//This command makes the display enter partial mode
		EPD_W21_WriteCMD(0x90);		//resolution setting
		EPD_W21_WriteDATA (x_start/256);
		EPD_W21_WriteDATA (x_start%256);   //x-start    
		
		EPD_W21_WriteDATA (x_end/256);		
		EPD_W21_WriteDATA (x_end%256-1);  //x-end	
	
		EPD_W21_WriteDATA (y_start/256);  //
		EPD_W21_WriteDATA (y_start%256);   //y-start    
		
		EPD_W21_WriteDATA (y_end/256);		
		EPD_W21_WriteDATA (y_end%256-1);  //y-end
		EPD_W21_WriteDATA (0x01);	

	 EPD_W21_WriteCMD(0x13);				 //writes New data to SRAM.
		for(i=0;i<count;i++)	     
	 {
		EPD_W21_WriteDATA(new_data[i]);  
		
	 }
		
	
    	
	EPD_W21_WriteCMD(0x12);		 //DISPLAY REFRESH 		             
	delay_xms(10);     //!!!The delay here is necessary, 200uS at least!!!     
	lcd_chkstatus();
	EPD_W21_WriteCMD(0X92);  	//This command makes the display exit partial mode and enter normal mode. 
 
}	
//UC8179
void EPD_init_GUI(void)
{	
	  unsigned int i;
		EPD_W21_Init();			//Electronic paper IC reset	

		EPD_W21_WriteCMD(0x04); //POWER ON
		lcd_chkstatus();        //waiting for the electronic paper IC to release the idle signal
	
		EPD_W21_WriteCMD(0X00);			//PANNEL SETTING
		EPD_W21_WriteDATA(0x0F);   //KW-3f   KWR-2F	BWROTP 0f	BWOTP 1f
	

		EPD_W21_WriteCMD(0X50);			//VCOM AND DATA INTERVAL SETTING
		EPD_W21_WriteDATA(0x20);
		EPD_W21_WriteDATA(0x07);
		
		EPD_W21_WriteCMD(0x10);	       //Transfer old data
	  for(i=0;i<48000;i++)	  
    {	
	  EPD_W21_WriteDATA(0x00); 
    }
		EPD_W21_WriteCMD(0x13);		     //Transfer new data
	  for(i=0;i<48000;i++)	     
	  {
	  EPD_W21_WriteDATA(0x00);  //Transfer the actual displayed data
	  }
}
void EPD_sleep(void)
{
		EPD_W21_WriteCMD(0X50);  //VCOM AND DATA INTERVAL SETTING			
		EPD_W21_WriteDATA(0xf7); //WBmode:VBDF 17|D7 VBDW 97 VBDB 57		WBRmode:VBDF F7 VBDW 77 VBDB 37  VBDR B7	

		EPD_W21_WriteCMD(0X02);  	//power off
	  lcd_chkstatus();          //waiting for the electronic paper IC to release the idle signal
		delay_xms(100);	  //!!!The delay here is necessary, 200uS at least!!!     	
	  EPD_W21_WriteCMD(0X07);  	//deep sleep
		EPD_W21_WriteDATA(0xA5);
}



void PIC_display(const unsigned char* picData)
{
    unsigned int i;
	  //Write Data
		EPD_W21_WriteCMD(0x10);	     
		for(i=0;i<48000;i++)	     
		{
			EPD_W21_WriteDATA(0x00); 
		}
		EPD_W21_WriteCMD(0x13);	     
		for(i=0;i<48000;i++)	     
		{		
			
      EPD_W21_WriteDATA(picData[i]);   			
		}
	 //Refresh
	  EPD_W21_WriteCMD(0x12);		//DISPLAY REFRESH 	
		delay_xms(1);	  //!!!The delay here is necessary, 200uS at least!!!     
		lcd_chkstatus();          //waiting for the electronic paper IC to release the idle signal


}
void EPD_SetRAMValue_BaseMap( const unsigned char * datas)
{
  unsigned int i;	
  EPD_W21_WriteCMD(0x10);  //write old data 
  for(i=0;i<48000;i++)
   {               
     EPD_W21_WriteDATA(0xFF); //is  different
   }
  EPD_W21_WriteCMD(0x13);  //write new data 
  for(i=0;i<48000;i++)
   {               
     EPD_W21_WriteDATA(datas[i]);
   }	 
	 //Refresh
	  EPD_W21_WriteCMD(0x12);		//DISPLAY REFRESH 	
		delay_xms(1);	  //!!!The delay here is necessary, 200uS at least!!!     
		lcd_chkstatus();          //waiting for the electronic paper IC to release the idle signal
		 
	 
}
/*
void PIC_display_Part(const unsigned char* picData)
{
    unsigned int i;
	  //Write Data
		EPD_W21_WriteCMD(0x10);	     
		for(i=0;i<48000;i++)	     
		{
			EPD_W21_WriteDATA(Old_data[i]); 
		}
		EPD_W21_WriteCMD(0x13);	     
		for(i=0;i<48000;i++)	     
		{		
			
      EPD_W21_WriteDATA(picData[i]);   
      Old_data[i]=picData[i];			
		}
	 //Refresh
	  EPD_W21_WriteCMD(0x12);		//DISPLAY REFRESH 	
		delay_xms(1);	  //!!!The delay here is necessary, 200uS at least!!!     
		lcd_chkstatus();          //waiting for the electronic paper IC to release the idle signal


}*/
void PIC_display_Clear(void)
{
    unsigned int i;
	  //Write Data
		EPD_W21_WriteCMD(0x10);	     
		for(i=0;i<48000;i++)	     
		{
			EPD_W21_WriteDATA(0x00);  
		}
		EPD_W21_WriteCMD(0x13);	     
		for(i=0;i<48000;i++)	     
		{
			EPD_W21_WriteDATA(0x00);  
		}
		//Refresh
  	EPD_W21_WriteCMD(0x12);		//DISPLAY REFRESH 	
		delay_xms(1);	             //!!!The delay here is necessary, 200uS at least!!!     
		lcd_chkstatus();          //waiting for the electronic paper IC to release the idle signal

}

void lcd_chkstatus(void)
{
	while(!isEPD_W21_BUSY); //0:BUSY, 1:FREE                     
}














/***********************************************************
						end file
***********************************************************/

