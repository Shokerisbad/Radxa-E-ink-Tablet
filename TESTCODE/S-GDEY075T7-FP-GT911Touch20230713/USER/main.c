#include "stm32f10x.h"
#include "delay.h"
#include "sys.h"
//EPD
#include "Display_EPD_W21_spi.h"
#include "Display_EPD_W21.h"
#include "Ap_29demo.h"	
//GUI
#include "GUI_Paint.h"
#include "fonts.h"
//TOUCH
#include "usart.h"
#include "iic.h"
#include "gt9xx.h"
#include "led.h"
int EpdNum;
//Tips//
/*
1.When the e-paper is refreshed in full screen, the picture flicker is a normal phenomenon, and the main function is to clear the display afterimage in the previous picture.
2.When the partial refresh is performed, the screen does not flash.
3.After the e-paper is refreshed, you need to put it into sleep mode, please do not delete the sleep command.
4.Please do not take out the electronic paper when power is on.
5.Wake up from sleep, need to re-initialize the e-paper.
6.When you need to transplant the driver, you only need to change the corresponding IO. The BUSY pin is the input mode and the others are the output mode.
*/
int	main(void)
{
  unsigned char i;
		delay_init();	    	     //Delay function initialization
		NVIC_Configuration(); 	//Set NVIC interrupt grouping 2
    EPD_GPIO_Init();       //EPD GPIO  initialization
		delay_init();	    	 
		NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2); 
		uart_init(115200);	 //115200
   // LED_Init();		  		//
    GT9XX_Initial();
    printf("Start Touch..."); 
    EPD_init_Fast(); //EPD init Fast
	  PIC_display_Clear();//EPD Clear
	  						
    EPD_init_Fast(); //EPD init Fast
    EPD_SetRAMValue_BaseMap(gImage_basemapT);//EPD_picture1
    EPD_display_init();
    EPD_partial_display(368,166,Num[0],104,48,1);  //x,y,old_data,new_data,W,L,mode 
    
  while(1) 
  { 
   if(Is_INT_IN==0) //Touch is OK
    {   
			TPR_Structure.TouchSta |= TP_COORD_UD;       
		if(TPR_Structure.TouchSta &TP_COORD_UD)   
			{
				TPR_Structure.TouchSta=0;  			
			  gt910_isr();  
	      if(TPR_Structure.x[0]!=0&&TPR_Structure.y[0]!=0)
				{
					 //printf("Is_INT_IN==0\r\n"); //FT6336U
					 printf("count:%d\r\n",touch_num); //print count
					 printf("x0:%d,y0:%d\r\n",TPR_Structure.x[0],TPR_Structure.y[0]); 
				}					
       switch(touch_num)
       {
          case 1:
             //num++
              if((TPR_Structure.x[0]!=0)&&(TPR_Structure.y[0]!=0)
									&&(TPR_Structure.x[0]>500)&&(TPR_Structure.x[0]<800) 
									&&(TPR_Structure.y[0]>0)&&(TPR_Structure.y[0]<480))
									{											
										if(EpdNum<9) 
											EpdNum++; 
										else	
											EpdNum=0;
										 printf("EpdNum=%d\r\n",EpdNum);    										 
										 EPD_partial_display(368,166,Num[EpdNum],104,48,1);  //x,y,old_data,new_data,W,L,mode 									
									}
              //num--
              else if((TPR_Structure.x[0]!=0)&&(TPR_Structure.y[0]!=0)
										&&(TPR_Structure.x[0]>0)&&(TPR_Structure.x[0]<300) 
										&&(TPR_Structure.y[0]>0)&&(TPR_Structure.y[0]<480))							
									 {     								
											if(EpdNum>0)
												EpdNum--;
											else
												 EpdNum=9;   							 
											printf("EpdNum=%d\r\n",EpdNum);  											 
											EPD_partial_display(368,166,Num[EpdNum],104,48,1);  //x,y,old_data,new_data,W,L,mode    
												
										} 
              break;
          case 2:
              if((TPR_Structure.x[0]!=0)&&(TPR_Structure.y[0]!=0)
                &&(TPR_Structure.x[1]!=0)&&(TPR_Structure.y[1]!=0))
              {
               printf("count:%d\r\n",touch_num); //print count
               printf("x0:%d,y0:%d\r\n",TPR_Structure.x[0],TPR_Structure.y[0]);
               printf("x1:%d,y1:%d\r\n",TPR_Structure.x[1],TPR_Structure.y[1]);
              }
              break;
          
          default:
            break;
            
          }
      //Clear touch Coordinate data
        for(i=0;i<2;i++)
        {
          TPR_Structure.x[i]=0;
          TPR_Structure.y[i]=0;
        }
              
      }
  }
   
 }
}	
	





















