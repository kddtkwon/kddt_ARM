#include "device_driver.h"

#define LCDW         (320)
#define LCDH         (240)
#define X_MIN        (0)
#define X_MAX        (LCDW - 1)
#define Y_MIN        (0)
#define Y_MAX        (LCDH - 1)

#define TIMER_PERIOD (10)
#define RIGHT        (1)
#define LEFT         (-1)
#define GROUND         (0)
#define SKY       (1)

#define CAR_STEP     (10)
#define POOP_SIZE_X   (20)
#define POOP_SIZE_Y   (20)
#define FROG_STEP    (10)
#define FROG_SIZE_X  (20)
#define FROG_SIZE_Y  (20)

#define BACK_COLOR       (5)
#define CAR_COLOR        (2)
#define FROG_COLOR       (4)
#define BLUE_POOP_COLOR  (3)

#define GAME_OVER    (1)
#define MAX_POOPS    (5)

#define POOP_TYPE_NORMAL (0)
#define POOP_TYPE_BONUS  (1)
#define SYSTEM_CLOCK (72000000UL)

#ifndef uint32_t
#define uint32_t unsigned int
#endif

#define DANGER_WARNING_TICKS 20   // 2초 (TIMER_PERIOD * 10 단위)
#define DANGER_DURATION_TICKS 30  // 3초 지속
#define DANGER_WIDTH  (FROG_SIZE_X * 3)
#define DANGER_HEIGHT LCDH
#define DANGER_COLOR_ACTIVE     (0) // GREEN index
#define DANGER_COLOR_WARNING    (1) // YELLOW index
#define DANGER_ACTIVATE_CHANCE 8

#define EMERGENCY_SCORE_TRIGGER 15
#define EMERGENCY_DURATION_TICKS 50
#define EMERGENCY_TOTAL_TICKS 100
#define SAFE_ZONE_X  120
#define SAFE_ZONE_Y (LCDH - SAFE_ZONE_H)
#define SAFE_ZONE_W   80
#define SAFE_ZONE_H   40

typedef struct {
    int x, y;
    int w, h;
    int ci;
    int dir;
    int type;
} QUERY_DRAW;
static int game_over = 0;
static int emergency_active = 0;
static int emergency_timer = 0;
static int emergency_warning_phase = 1;
static int emergency_mode_done = 0;
static int emergency_background_drawn = 0;

static int danger_active = 0;
static int danger_x = 0;
static int danger_timer = 0;

static QUERY_DRAW frog;
static QUERY_DRAW poops[MAX_POOPS];
static int score;
static float speed_multiplier = 1.0f;
static int last_speed_increase_score = 0;
static unsigned short color[] = { RED, YELLOW, GREEN, BLUE, WHITE, BLACK };

static uint32_t lcg_seed = 12345;
static const uint32_t LCG_A = 1103515245;
static const uint32_t LCG_C = 12345;
static const uint32_t LCG_M = 2147483647;
void Buzzer_Beep(unsigned char tone, int duration); 
enum key { C1, C1_, D1, D1_, E1, F1, F1_, G1, G1_, A1, A1_, B1, C2 };
enum note { N16 = 125, N8 = 250, N4 = 500 };

const unsigned char start_music[] = { C1, D1, E1, F1, G1, G1, G1, 0xFF };
const unsigned short start_note[]  = { N8, N8, N8, N8, N4, N8, N8, 0 };
static void Buzzer_GameOver_Music(void) {
    enum key { C1, D1, E1, F1, G1, A1, B1, C2 };
    enum note { N16 = 125, N8 = 250, N4 = 500 };
    unsigned char notes[] = { C2, B1, G1, E1, 0xFF };
    unsigned short durations[] = { N8, N8, N8, N4, 0 };
    int i = 0;
    while (notes[i] != 0xFF) {
        Buzzer_Beep(notes[i], durations[i]);
        TIM2_Delay(50);
        i++;
    }
}
static uint32_t simple_rand(void) {
    lcg_seed = (LCG_A * lcg_seed + LCG_C) % LCG_M;
    return lcg_seed;
}
static int Is_In_Safe_Zone(QUERY_DRAW *obj) {
    return (obj->x + obj->w > SAFE_ZONE_X &&
            obj->x < SAFE_ZONE_X + SAFE_ZONE_W &&
            obj->y + obj->h > SAFE_ZONE_Y &&
            obj->y < SAFE_ZONE_Y + SAFE_ZONE_H);
}


static void Draw_Safe_Zone(void) {
    Lcd_Draw_Box(SAFE_ZONE_X, SAFE_ZONE_Y, SAFE_ZONE_W, SAFE_ZONE_H, color[3]);
}



static void Trigger_Emergency_Mode(void) {
    emergency_active = 1;
    emergency_timer = 0;
    emergency_warning_phase = 1;
    emergency_mode_done = 0;
    emergency_background_drawn = 0;
    Lcd_Draw_Box(80, 100, 160, 40, color[BACK_COLOR]);
    Lcd_Printf(100, 110, RED, BACK_COLOR, 2, 2, "EMERGENCY");
    Draw_Safe_Zone();
}
static int Update_Emergency_Mode(void) {
    if (!emergency_active) return 0;

    emergency_timer++;

    // 경고 단계
    if (emergency_timer < EMERGENCY_DURATION_TICKS) {
        emergency_warning_phase = 1;
        emergency_background_drawn = 0;
        Draw_Safe_Zone();
        Lcd_Draw_Box(80, 100, 160, 40, color[BACK_COLOR]);
        Lcd_Printf(100, 110, RED, BACK_COLOR, 2, 2, "EMERGENCY");
        return 0;
    }

    // 즉시종료 패턴
    emergency_warning_phase = 0;

    if (!emergency_background_drawn) {
        Lcd_Clr_Screen();
        Lcd_Draw_Box(0, 0, LCDW, LCDH, RED);
        emergency_background_drawn = 1;
    }

    Draw_Safe_Zone();

    // 즉시종료료 조건
    if (!Is_In_Safe_Zone(&frog)) {
        emergency_active = 0;
        emergency_mode_done = 1;
        return GAME_OVER;  // 여기!
    }

    // 종료 조건
    if (emergency_timer >= EMERGENCY_TOTAL_TICKS) {
        emergency_active = 0;
        emergency_mode_done = 1;
        emergency_warning_phase = 1;
        emergency_background_drawn = 0;
        Lcd_Clr_Screen();
    }

    return 0;
}






static void Draw_Danger_Zone(void) {
    int color_index = (danger_timer < DANGER_WARNING_TICKS) ? DANGER_COLOR_WARNING : DANGER_COLOR_ACTIVE;
    Lcd_Draw_Box(danger_x, 0, DANGER_WIDTH, DANGER_HEIGHT, color[color_index]);
    Lcd_Draw_Box(10, 10, 10, 10, color[color_index]); // 디버그용

    // 이전 텍스트 영역을 백그라운드 색으로 덮어줌 (문자 길이 충분히 커버되게)
    Lcd_Draw_Box(100, 100, 80, 16, color[BACK_COLOR]);

    if (danger_timer < DANGER_WARNING_TICKS) {
        Lcd_Printf(100, 100, RED, BACK_COLOR, 1, 1, "WARNING");
    } else {
        Lcd_Printf(100, 100, RED, BACK_COLOR, 1, 1, "DANGER");
    }
}
static void Activate_Danger(void) {
    danger_active = 1;
    danger_timer = 0;
    danger_x = simple_rand() % (X_MAX - DANGER_WIDTH);
    Uart_Printf("[danger zone generate] X=%d~%d (Y all)\n", danger_x, danger_x + DANGER_WIDTH);
}

static int Update_Danger_Zone(void) {
    if (emergency_active) return 0; // 무적존 도중엔 Danger Zone 비활성화

    if (!danger_active) {
        if (simple_rand() % DANGER_ACTIVATE_CHANCE == 0) {
            Activate_Danger();
        }
        return 0;
    }

    danger_timer++;
    Draw_Danger_Zone();

    if (danger_timer >= DANGER_WARNING_TICKS) {
        if (frog.x < danger_x + DANGER_WIDTH && frog.x + frog.w > danger_x) {
            return GAME_OVER;
        }
    }

    if (danger_timer >= DANGER_DURATION_TICKS) {
        danger_active = 0;
        Lcd_Draw_Box(danger_x, 0, DANGER_WIDTH, DANGER_HEIGHT, color[BACK_COLOR]);
        Lcd_Draw_Box(100, 100, 100, 20, color[BACK_COLOR]);
    }

    return 0;
}


static void Relocate_Poop(int index) {
    // 기존 자리 지우기
    Lcd_Draw_Box(poops[index].x, poops[index].y, poops[index].w, poops[index].h, color[BACK_COLOR]);
    // 위치 재배치
    poops[index].y = -(simple_rand() % LCDH) - POOP_SIZE_Y;
    poops[index].x = simple_rand() % (X_MAX - POOP_SIZE_X);
} 


static int Check_Collision(void) {
    int i;
    for (i = 0; i < MAX_POOPS; i++) {
        if ((poops[i].y >= 0) && (poops[i].x < frog.x + frog.w) && (poops[i].x + poops[i].w > frog.x) &&
            (poops[i].y < frog.y + frog.h) && (poops[i].y + poops[i].h > frog.y)) {
            if (poops[i].type == POOP_TYPE_BONUS) {
                score += 5;
                Uart_Printf("BONUS! SCORE = %d\n", score);
                Lcd_Printf(0, 0, BLUE, WHITE, 2, 2, "%d", score);
                Relocate_Poop(i);
            } else {
                Uart_Printf("CRASHED! SCORE = %d\n", score);
                return GAME_OVER;
            }
        }
    }
    return 0;
}

static int Move_Poops(void) {
    if (emergency_active) return 0;
    static int poop_spawn_delay = 0;
    static int poops_on_screen = 0;
    static int score_threshold = 5;
    int i;
    for (i = 0; i < MAX_POOPS; i++) {
        if (poops[i].y >= 0 && poops[i].y < LCDH)
            Lcd_Draw_Box(poops[i].x, poops[i].y, poops[i].w, poops[i].h, color[BACK_COLOR]);

        poops[i].y += CAR_STEP * speed_multiplier;

        if (poops[i].y > Y_MAX) {
            poops[i].y = -(simple_rand() % LCDH) - POOP_SIZE_Y;
            poops[i].x = simple_rand() % (X_MAX - POOP_SIZE_X);

            if (simple_rand() % 5 == 0) {
                poops[i].ci = BLUE_POOP_COLOR;
                poops[i].type = POOP_TYPE_BONUS;
            } else {
                poops[i].ci = CAR_COLOR;
                poops[i].type = POOP_TYPE_NORMAL;
            }

            poops_on_screen--;

            if (poops[i].type == POOP_TYPE_NORMAL) {
                score++;
                Lcd_Printf(0, 0, BLUE, WHITE, 2, 2, "%d", score);
            }

            if (score >= last_speed_increase_score + score_threshold) {
                speed_multiplier *= 1.1f;
                Uart_Printf("Speed increased to %.2f\n", speed_multiplier);
                last_speed_increase_score = score;
            }
        }

        if (poops[i].y >= 0 && poops[i].y < LCDH)
            Lcd_Draw_Box(poops[i].x, poops[i].y, poops[i].w, poops[i].h, color[poops[i].ci]);
    }

    poop_spawn_delay++;
    if (poop_spawn_delay > 50 && poops_on_screen < MAX_POOPS) {
        int new_poop_index = simple_rand() % MAX_POOPS;
        if (poops[new_poop_index].y < -POOP_SIZE_Y) {
            poops[new_poop_index].y = 0;
            poops[new_poop_index].x = simple_rand() % (X_MAX - POOP_SIZE_X);
            if (simple_rand() % 5 == 0) {
                poops[new_poop_index].ci = BLUE_POOP_COLOR;
                poops[new_poop_index].type = POOP_TYPE_BONUS;
            } else {
                poops[new_poop_index].ci = CAR_COLOR;
                poops[new_poop_index].type = POOP_TYPE_NORMAL;
            }
            poops_on_screen++;
        }
        poop_spawn_delay = 0;
    }

    return Check_Collision();
}


static void k0(void) { if (frog.y > Y_MIN) frog.y -= FROG_STEP; }
static void k1(void) { if (frog.y + frog.h < Y_MAX) frog.y += FROG_STEP; }
static void k2(void) { if (frog.x > X_MIN) frog.x -= FROG_STEP; }
static void k3(void) { if (frog.x + frog.w < X_MAX) frog.x += FROG_STEP; }

static int Frog_Move(int k) {
    static void (*key_func[])(void) = {k0, k1, k2, k3};
    if (k <= 3) key_func[k]();
    return Check_Collision();
}

static void Game_Init(void) {
    score = 0;
    speed_multiplier = 1.0f;
    last_speed_increase_score = 0;
    Lcd_Clr_Screen();
    emergency_active = 0;
    emergency_timer = 0;
    danger_active = 0;
    danger_timer = 0;
    danger_x = 0;
    emergency_warning_phase = 1;
    emergency_mode_done = 0;
    game_over = 0;
    frog = (QUERY_DRAW){150, 220, FROG_SIZE_X, FROG_SIZE_Y, FROG_COLOR, GROUND};
    Lcd_Draw_Box(frog.x, frog.y, frog.w, frog.h, color[frog.ci]);
    Lcd_Printf(0, 0, BLUE, WHITE, 2, 2, "%d", score);
    int i;
    for (i = 0; i < MAX_POOPS; i++) {
        poops[i].x = simple_rand() % (X_MAX - POOP_SIZE_X);
        poops[i].y = -(i * (LCDH / MAX_POOPS)) - POOP_SIZE_Y;
        if (simple_rand() % 5 == 0) {
            poops[i].ci = BLUE_POOP_COLOR;
            poops[i].type = POOP_TYPE_BONUS;
        } else {
            poops[i].ci = CAR_COLOR;
            poops[i].type = POOP_TYPE_NORMAL;
        }
        poops[i].w = POOP_SIZE_X;
        poops[i].h = POOP_SIZE_Y;

        if (poops[i].y >= 0 && poops[i].y < LCDH)
            Lcd_Draw_Box(poops[i].x, poops[i].y, poops[i].w, poops[i].h, color[poops[i].ci]);
    }

  
}

static void Draw_Object(QUERY_DRAW *obj) {
    Lcd_Draw_Box(obj->x, obj->y, obj->w, obj->h, color[obj->ci]);
}

void Buzzer_Beep(unsigned char tone, int duration) {
    static const unsigned short tone_table[] = {
        261, 277, 293, 311, 329, 349, 369,
        391, 415, 440, 466, 493, 523
    };

    if (tone >= sizeof(tone_table)/sizeof(tone_table[0])) return;

    Macro_Set_Bit(RCC->APB2ENR, 3);
    Macro_Write_Block(GPIOB->CRL, 0xF, 0xB, 0);
    RCC->APB1ENR |= (1 << 1);

    TIM3->PSC = (unsigned int)((SYSTEM_CLOCK / 10000) / tone_table[tone]) - 1;
    TIM3->ARR = 10000 - 1;
    TIM3->CCR3 = TIM3->ARR / 2;

    TIM3->CCMR2 &= ~(0xFF);
    TIM3->CCMR2 |= 0x0068;
    TIM3->CCER |= (1 << 8);

    Macro_Set_Bit(TIM3->EGR, 0);
    TIM3->CR1 |= 0x01;

    TIM2_Delay(duration);

    TIM3->CCR3 = 0;
    TIM3->CR1 &= ~(1 << 0);
}

void Play_Start_Music(void) {
    int i = 0;
    while (start_music[i] != 0xFF) {
        Buzzer_Beep(start_music[i], start_note[i]);
        TIM2_Delay(50);
        i++;
    }
}

extern volatile int TIM4_expired;
extern volatile int USART1_rx_ready;
extern volatile int USART1_rx_data;
extern volatile int Jog_key_in;
extern volatile int Jog_key;

void System_Init(void) {
    Clock_Init();
    LED_Init();
    Key_Poll_Init();
    Uart1_Init(115200);
    SCB->VTOR = 0x08003000;
    SCB->SHCSR = 7 << 16;
}

void TIM4_IRQHandler(void) {
    TIM4_expired = 1;
    Macro_Clear_Bit(TIM4->SR, 0);
}

#define DIPLAY_MODE 3
void Main(void) {
    System_Init();
    Uart_Printf("OUCH IT'S POOP\n");

    Lcd_Init(DIPLAY_MODE);
    Jog_Poll_Init();
    Jog_ISR_Enable(1);
    Uart1_RX_Interrupt_Enable(1);

    for (;;) {
        Game_Init();
        Play_Start_Music();
        TIM4_Repeat_Interrupt_Enable(1, TIMER_PERIOD * 10);
        Lcd_Printf(0, 0, BLUE, WHITE, 2, 2, "%d", score);

        int game_over = 0;
        while (!game_over) {
            if (Jog_key_in) {
                frog.ci = BACK_COLOR;
                Draw_Object(&frog);
                game_over = Frog_Move(Jog_key);
                frog.ci = FROG_COLOR;
                Draw_Object(&frog);
                Jog_key_in = 0;
            }

            if (TIM4_expired) {
                game_over = Move_Poops();
            
                if (!emergency_active && !emergency_mode_done && score >= EMERGENCY_SCORE_TRIGGER) {
                    Trigger_Emergency_Mode();
                }
            
                if (!game_over)  // 아직 game over 안됐을 때만 수행
                    game_over = Update_Emergency_Mode();  // 이게 핵심!!!
            
                if (!game_over)
                    game_over = Update_Danger_Zone();
            
                TIM4_expired = 0;
            }

            if (game_over) {
                TIM4_Repeat_Interrupt_Enable(0, 0);
                TIM3->CCR3 = 0;
                Macro_Clear_Bit(TIM3->CR1, 0);
                Buzzer_GameOver_Music();
                Uart_Printf("Game Over, Please press any key to continue.\n");
                Jog_Wait_Key_Pressed();
                Jog_Wait_Key_Released();
                Uart_Printf("Game Start\n");
                emergency_active = 0;
                emergency_timer = 0;
                emergency_mode_done = 0;
                emergency_warning_phase = 1;
                emergency_background_drawn = 0;
                break;
            }
        }
    }

}
