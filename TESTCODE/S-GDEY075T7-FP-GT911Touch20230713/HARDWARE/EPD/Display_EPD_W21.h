#ifndef _DISPLAY_EPD_W21_H_
#define _DISPLAY_EPD_W21_H_


#define EPD_WIDTH   800
#define EPD_HEIGHT  480

//EPD
void EPD_W21_Init(void);
void EPD_init(void);
void PIC_display (const unsigned char* picData);
void EPD_sleep(void);
void EPD_refresh(void);
void lcd_chkstatus(void);
void PIC_display_Clear(void);
//Display canvas function
void EPD_Display(unsigned char *Image); 
void SetFrameMemory(
        const unsigned char* image_buffer,
        int x,
        int y,
        int image_width,
        int image_height
);
void EPD_init_GUI(void);				
void EPD_Refresh(void);			
void EPD_display_init(void);		
void EPD_partial_display(int x_start,int y_start,const unsigned char *new_data,unsigned int PART_COLUMN,unsigned int PART_LINE,unsigned char mode); //partial display 
void EPD_init_Fast(void);	
void PIC_display_Part(const unsigned char* picData);	
void EPD_SetRAMValue_BaseMap( const unsigned char * datas);				
#endif
/***********************************************************
						end file
***********************************************************/


