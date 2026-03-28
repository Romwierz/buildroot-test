//Definitions of 32-bit registers
#define TIM1_ID  	0
#define TIM1_STAT	1
#define TIM1_DIVL  	2
#define TIM1_DIVH  	3
#define TIM1_CNTL	4
#define TIM1_CNTH	5
//Number of registers
#define TIM1_REGS_NUM	6

#define MY_PAGE_SIZE 0x1000

typedef struct {
  uint32_t id;
  uint32_t stat;
  uint32_t divl;
  uint32_t divh;
  uint32_t cntl;
  uint32_t cnth;
} __attribute__((aligned(4))) WzTim1Regs;
