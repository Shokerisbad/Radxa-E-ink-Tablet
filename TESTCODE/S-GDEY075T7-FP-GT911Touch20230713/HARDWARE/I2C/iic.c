#include "iic.h"

#define IIC_PORT_SCL    GPIOE                  
#define IIC_PIN_SCL     GPIO_Pin_10             
#define IIC_RCC_SCL      RCC_APB2Periph_GPIOE   

#define IIC_PORT_SDA    GPIOB                   
#define IIC_PIN_SDA     GPIO_Pin_10            
#define IIC_RCC_SDA      RCC_APB2Periph_GPIOB 

#define SDA_H()    GPIO_SetBits(GPIOB,GPIO_Pin_10);
#define SDA_L()    GPIO_ResetBits(GPIOB,GPIO_Pin_10);

#define SCL_H()    GPIO_SetBits(GPIOE,GPIO_Pin_10);
#define SCL_L()    GPIO_ResetBits(GPIOE,GPIO_Pin_10);



static void iic_sda_out(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10 ;  
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;	   	
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure);		
}

static void iic_sda_in(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;  
  GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
 	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &GPIO_InitStructure);/**/
}

void IIC_delay(int dl)
{
	int i;
	while (dl--)
	{
		for (i = 0; i < 5; i++);
	}
}

#define IIC_Delay_COUNT 5

void iic_start(void)
{
	iic_sda_out();
    SDA_H();
    SCL_H();
    IIC_delay( IIC_Delay_COUNT );

    SDA_L();
    IIC_delay( IIC_Delay_COUNT );
    SCL_L();
    IIC_delay( IIC_Delay_COUNT );
}

void iic_stop(void)
{
    SCL_L();
    IIC_delay( IIC_Delay_COUNT );
    SDA_L();
    IIC_delay( IIC_Delay_COUNT );
    SCL_H();
    IIC_delay( IIC_Delay_COUNT );
    SDA_H();
    IIC_delay( IIC_Delay_COUNT );
}

void iic_ack(void)
{
    iic_sda_out();
	//SCL_L();
	//delay(10);
	SDA_L();
	IIC_delay( IIC_Delay_COUNT );	
	SCL_H();
	IIC_delay( IIC_Delay_COUNT );
	SCL_L();
	IIC_delay( IIC_Delay_COUNT );
}

void iic_nack(void)
{
	SCL_L();
	iic_sda_out();
	IIC_delay( IIC_Delay_COUNT );
	SDA_H();
	//I2C_SCL_L();
	IIC_delay( IIC_Delay_COUNT );
	SCL_H();
	IIC_delay( IIC_Delay_COUNT );
	SCL_L();
	IIC_delay( IIC_Delay_COUNT );
}

uint8_t iic_wait_ack(void)
{

  SCL_L();
	IIC_delay( IIC_Delay_COUNT );
	SDA_H();
	IIC_delay( IIC_Delay_COUNT );
	SCL_H();
	iic_sda_in();	
	IIC_delay( IIC_Delay_COUNT );
  if((IIC_PORT_SDA->IDR & IIC_PIN_SDA) != (uint32_t)Bit_RESET)
	{
		SCL_L();
		return (E_Error);
  }
	else
	{
		SCL_L();
		return (E_Ok);
  }
	
	
	
}

uint8_t iic_write_byte(uint8_t dat, uint8_t cack)
{
	uint32_t i;

	iic_sda_out();
	for (i = 0; i < 8; i++)
	{
		SCL_L();
		IIC_delay( IIC_Delay_COUNT );
		
		if (dat & 0x80)
		{
		    SDA_H();
		}
		else
		{
		    SDA_L();
		}
		IIC_delay( IIC_Delay_COUNT );
		
		dat <<= 1;
		
		SCL_H();
		IIC_delay( IIC_Delay_COUNT );

		SCL_L();
	}
	if (cack)
	{
		return (iic_wait_ack()); 
	}
	else
	{
		return 0;
	}
}

uint8_t iic_read_byte(uint8_t ack)
{
	uint8_t temp;
	uint32_t i;
	
	SDA_H();
	SCL_L();
	IIC_delay( IIC_Delay_COUNT );

	iic_sda_in();
	
	for (i = 0; i < 8; i++)
	{
		temp <<= 1;
		
		SCL_H();
		IIC_delay( IIC_Delay_COUNT );
		if (GPIO_ReadInputDataBit(IIC_PORT_SDA,IIC_PIN_SDA) == 1)
		{
			temp |= 0x01;
		}
		else
		{
			temp &= ~(0x01);
		}
		SCL_L();
		IIC_delay( IIC_Delay_COUNT );
	}
	if (ack)
	{
		//iic_sda_out();
		iic_ack();
	}
	else
	{
		iic_nack();
	}
	return (temp); 
}
void iic_init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;					

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB|RCC_APB2Periph_GPIOE,ENABLE);

  // PE10:复位SCL 
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;	//PE10
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;		//推挽输出
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;		//IO口速度为50MHz	
	GPIO_Init(GPIOE,&GPIO_InitStructure);					//初始化对应GPIOE		
	GPIO_ResetBits(GPIOE,GPIO_Pin_10);							//PD10 输出高	
		/****** 	//PB10:数据SDA  ***************************************/	
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;	//PB10
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;		//推挽输出
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;		//IO口速度为50MHz	
	GPIO_Init(GPIOB,&GPIO_InitStructure);					//初始化对应GPIOE		
	GPIO_ResetBits(GPIOB,GPIO_Pin_10);							//PB10 输出高

}



