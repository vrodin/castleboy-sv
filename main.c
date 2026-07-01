/* main.c - entry point + state machine for the CastleBoy Supervision port.
 * CastleBoy (c) 2016 dir3kt/jlauener (MIT). Port glue + platform layer original. */
#include "castleboy.h"
#include "assets.h"

extern u8 game_levelIndex;
u8 game_numLevels(void);

static u8 stateTimer;

static void draw_logo(void)
{
    spr_overwrite(49, 14, logo, 0);
}

void main(void)
{
    u8 prevState = 0xFF;

    sv_init();
    game_reset();
    mainState = STATE_TITLE;

    for (;;) {
        poll_buttons();

        if (mainState != prevState) { stateTimer = 0; prevState = mainState; }
        else if (stateTimer < 255) ++stateTimer;

        if (mainState == STATE_PLAY) plat_play_begin();
        else                         plat_menu_begin();

        switch (mainState) {
            case STATE_TITLE:
                draw_logo();
                if ((g_frame >> 4) & 1)
                    draw_number(64, 50, game_score, ALIGN_CENTER); /* show last score */
                if (just_pressed(BTN_A) || just_pressed(BTN_START)) {
                    game_reset();
                    mainState = STATE_STAGE_INTRO;
                }
                break;

            case STATE_STAGE_INTRO:
                /* show the current level number, then begin */
                draw_number(64, 28, (u16)(game_levelIndex + 1), ALIGN_CENTER);
                if (stateTimer > 45 || just_pressed(BTN_A) || just_pressed(BTN_START))
                    game_play();   /* -> STATE_PLAY */
                break;

            case STATE_PLAY:
                game_loop();
                break;

            case STATE_LEVEL_FINISHED:
                draw_number(64, 28, game_score, ALIGN_CENTER);
                if (stateTimer > 50) {
                    if (game_levelIndex >= game_numLevels())
                        mainState = STATE_GAME_FINISHED;
                    else
                        mainState = STATE_STAGE_INTRO;
                }
                break;

            case STATE_PLAYER_DIED:
                if (stateTimer > 45) {
                    if (game_life > 0) game_play();      /* retry same level */
                    else mainState = STATE_GAME_OVER;
                }
                break;

            case STATE_GAME_OVER:
                draw_number(64, 28, game_score, ALIGN_CENTER);
                if (stateTimer > 30 && (just_pressed(BTN_A) || just_pressed(BTN_START)))
                    mainState = STATE_TITLE;
                break;

            case STATE_GAME_FINISHED:
                draw_logo();
                draw_number(64, 50, game_score, ALIGN_CENTER);
                if (stateTimer > 30 && (just_pressed(BTN_A) || just_pressed(BTN_START)))
                    mainState = STATE_TITLE;
                break;

            default:
                mainState = STATE_TITLE;
                break;
        }

        sv_display();
        ++g_frame;
    }
}
