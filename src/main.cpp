#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <tice.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <graphx.h>
#include <keypadc.h>
#include <fileioc.h>

//! Configs

//? Keybinds
#define KEY_LANE_1      kb_KeyMath
#define KEY_LANE_2      kb_KeyApps
#define KEY_LANE_3      kb_KeyPrgm
#define KEY_LANE_4      kb_KeyVars
#define KEY_LANE_5      kb_KeyClear
#define KEY_EXIT        kb_KeyEnter

//? Game settings
#define LANE_COUNT      5
#define LANE_WIDTH      40  
#define LANE_START_X    60  // (320 - (5 * 40)) / 2 = 60 to center it
#define HIT_LINE_Y      180
#define NOTE_HEIGHT     20
#define NOTE_SPEED      3
#define HIT_WINDOW      15 
#define MAX_NOTES       2500 // Increased slightly for 5 lanes

//? Colors
#define COL_GREEN       0x06
#define COL_ORANGE      0xE5 
#define COL_BLACK       0x00
#define COL_WHITE       0xFF
#define COL_PURPLE      0x14
#define COL_GRAY        0xE0

//! Data Structures
typedef struct {
    uint24_t timestamp;
    uint8_t  lane;      // 0 to 4
} Note;

typedef struct {
    int x, y;
    int score_val;
    int life; 
    uint8_t color;
} ScoreEffect;

//! Global Settings
Note song_buffer[MAX_NOTES];
uint24_t note_count = 0;
uint24_t high_score = 0; 

#define MAX_EFFECTS 12
ScoreEffect effects[MAX_EFFECTS];

uint8_t note_sprite_data[2 + LANE_WIDTH * NOTE_HEIGHT];
gfx_sprite_t *note_sprite = (gfx_sprite_t*)note_sprite_data;

//! Graphics
void init_graphics() {
    note_sprite->width = LANE_WIDTH;
    note_sprite->height = NOTE_HEIGHT;
    memset(note_sprite->data, COL_PURPLE, LANE_WIDTH * NOTE_HEIGHT);
    
    //* Note border
    for(int y = 0; y < NOTE_HEIGHT; y++) {
        for(int x = 0; x < LANE_WIDTH; x++) {
            if (x==0 || x==LANE_WIDTH-1 || y==0 || y==NOTE_HEIGHT-1) {
                note_sprite->data[y*LANE_WIDTH+x] = COL_BLACK;
            }
        }
    }
    //* Reset effects
    for(int i=0; i<MAX_EFFECTS; i++) effects[i].life = 0;
}

void draw_ui() {
    gfx_SetColor(COL_GRAY);
    //* Draw vertical dividers for all lanes
    for(int i=0; i <= LANE_COUNT; i++) {
        gfx_VertLine(LANE_START_X + (i * LANE_WIDTH), 0, 240);
    }
    
    //* Draw Hit Line
    gfx_SetColor(COL_BLACK);
    gfx_HorizLine(LANE_START_X, HIT_LINE_Y, LANE_COUNT * LANE_WIDTH);
}

//! Effect System

void spawn_effect(int x, int y, int score) {
    for(int i=0; i<MAX_EFFECTS; i++) {
        if (effects[i].life <= 0) {
            effects[i].x = x + (LANE_WIDTH/2) - 8; // Center text roughly
            effects[i].y = y - 10;
            effects[i].score_val = score;
            effects[i].life = 20; 
            effects[i].color = (score >= 9) ? COL_GREEN : COL_ORANGE;
            return;
        }
    }
}

void draw_effects() {
    for(int i=0; i<MAX_EFFECTS; i++) {
        if (effects[i].life > 0) {
            gfx_SetTextFGColor(effects[i].color);
            gfx_SetTextXY(effects[i].x, effects[i].y);
            gfx_PrintString("+");
            gfx_PrintInt(effects[i].score_val, 0);
            
            effects[i].y--; // Float up
            effects[i].life--;
        }
    }
    gfx_SetTextFGColor(COL_BLACK); // Reset text color
}

// --- FILE I/O ---

void get_filename(char* buffer, uint8_t slot) {
    sprintf(buffer, "Song%d", slot);
}

void save_map(uint8_t slot, uint24_t new_high_score) {
    char name[10];
    get_filename(name, slot);
    
    //'w' overwrites data
    uint8_t handle = ti_Open(name, "w");
    if (!handle) return;
    
    // Header: [Count] [HighScore]
    ti_Write(&note_count, sizeof(uint24_t), 1, handle);
    ti_Write(&new_high_score, sizeof(uint24_t), 1, handle);
    
    // Body: [Note Array]
    ti_Write(song_buffer, sizeof(Note), note_count, handle);
    
    ti_SetArchiveStatus(true, handle);
    ti_Close(handle);
}

bool load_map(uint8_t slot) {
    char name[10];
    get_filename(name, slot);
    
    uint8_t handle = ti_Open(name, "r");
    if (!handle) return false;
    
    ti_Read(&note_count, sizeof(uint24_t), 1, handle);
    
    //Read High Score
    if(ti_Read(&high_score, sizeof(uint24_t), 1, handle) != 1) {
        high_score = 0; 
    }

    if(note_count > MAX_NOTES) note_count = MAX_NOTES;
    ti_Read(song_buffer, sizeof(Note), note_count, handle);
    
    ti_Close(handle);
    return true;
}

// Peek at high score without loading the whole song
int32_t peek_high_score(uint8_t slot) {
    char name[10];
    get_filename(name, slot);
    uint8_t handle = ti_Open(name, "r");
    if (!handle) return -1; // Empty slot
    
    uint24_t temp_cnt, temp_score;
    ti_Read(&temp_cnt, sizeof(uint24_t), 1, handle);
    if(ti_Read(&temp_score, sizeof(uint24_t), 1, handle) != 1) {
        temp_score = 0;
    }
    ti_Close(handle);
    return (int32_t)temp_score;
}

//! input and menus

// Reads the 5 lane keys into a bool array
void scan_lane_keys(bool *keys) {
    kb_Scan();
    keys[0] = kb_IsDown(KEY_LANE_1);
    keys[1] = kb_IsDown(KEY_LANE_2);
    keys[2] = kb_IsDown(KEY_LANE_3);
    keys[3] = kb_IsDown(KEY_LANE_4);
    keys[4] = kb_IsDown(KEY_LANE_5);
}

uint8_t slot_menu(const char* title) {
    while(1) {
        kb_Scan();
        gfx_FillScreen(COL_WHITE);
        gfx_PrintStringXY(title, 100, 50);
        
        for(int i=1; i<=4; i++) {
            gfx_SetTextXY(120, 80 + (i*20));
            gfx_PrintString("["); gfx_PrintInt(i, 0); gfx_PrintString("] Slot "); 
            gfx_PrintInt(i, 0);
            
            int32_t score = peek_high_score(i);
            if(score == -1) gfx_PrintString(" (Empty)");
            else { gfx_PrintString(" - HS: "); gfx_PrintInt(score, 0); }
        }
        
        gfx_PrintStringXY("[Enter] Cancel", 120, 200);
        gfx_SwapDraw();

        if (kb_IsDown(kb_Key1)) { while(kb_IsDown(kb_Key1)) kb_Scan(); return 1; }
        if (kb_IsDown(kb_Key2)) { while(kb_IsDown(kb_Key2)) kb_Scan(); return 2; }
        if (kb_IsDown(kb_Key3)) { while(kb_IsDown(kb_Key3)) kb_Scan(); return 3; }
        if (kb_IsDown(kb_Key4)) { while(kb_IsDown(kb_Key4)) kb_Scan(); return 4; }
        if (kb_IsDown(KEY_EXIT)) { while(kb_IsDown(KEY_EXIT)) kb_Scan(); return 0; }
    }
}

//! record
void workflow_record() {
    uint24_t current_time = 0;
    note_count = 0;
    bool keys_prev[LANE_COUNT] = {0};
    bool keys_curr[LANE_COUNT] = {0};

    // 1. Record Loop
    while (1) {
        scan_lane_keys(keys_curr);
        if (kb_IsDown(KEY_EXIT)) {
            while(kb_IsDown(KEY_EXIT)) kb_Scan();
            break; 
        }

        for(int i=0; i<LANE_COUNT; i++) {
            if (keys_curr[i] && !keys_prev[i]) {
                if (note_count < MAX_NOTES) {
                    song_buffer[note_count].timestamp = current_time;
                    song_buffer[note_count].lane = i;
                    note_count++;
                }
            }
            keys_prev[i] = keys_curr[i];
        }

        gfx_FillScreen(COL_WHITE);
        draw_ui();
        gfx_PrintStringXY("RECORDING...", 10, 10);
        gfx_PrintStringXY("Press Enter to Finish", 10, 25);
        
        // Draw Upward
        for(int i = note_count - 1; i >= 0; i--) {
            int age = current_time - song_buffer[i].timestamp;
            int y = HIT_LINE_Y - (age * NOTE_SPEED);
            if (y < -20) break;
            if (y < 240) gfx_TransparentSprite(note_sprite, LANE_START_X + (song_buffer[i].lane * LANE_WIDTH), y);
        }
        gfx_SwapDraw();
        current_time++;
    }

    // 2. Save
    uint8_t slot = slot_menu("SAVE RECORDING TO:");
    if (slot != 0) {
        // !!! IMPORTANT: Reset high score to 0 when saving a new map !!!
        save_map(slot, 0); 
        gfx_PrintStringXY("Saved & Score Reset!", 100, 100);
        gfx_SwapDraw();
        delay(800);
    }
}

//! play
void workflow_play() {
    uint8_t slot = slot_menu("PLAY SLOT:");
    if (slot == 0) return;

    if (!load_map(slot)) {
        gfx_PrintStringXY("Slot Empty!", 130, 100);
        gfx_SwapDraw();
        delay(1000);
        return;
    }

    uint24_t current_time = 0;
    int map_head = 0;
    int current_score = 0;
    bool keys_prev[LANE_COUNT] = {0};
    bool keys_curr[LANE_COUNT] = {0};
    
    init_graphics(); 

    while (1) {
        scan_lane_keys(keys_curr);
        if (kb_IsDown(KEY_EXIT)) {
            while(kb_IsDown(KEY_EXIT)) kb_Scan();
            return;
        }

        // Cleanup Missed Notes
        while(map_head < note_count) {
            int time_diff = (int)song_buffer[map_head].timestamp - (int)current_time;
            int y = HIT_LINE_Y - (time_diff * NOTE_SPEED);
            if (y > 240) map_head++; 
            else break;
        }

        // Check Hits
        for(int i = map_head; i < map_head + 6 && i < note_count; i++) {
            Note *n = &song_buffer[i];
            if (n->timestamp == 0) continue; 

            int time_diff = (int)n->timestamp - (int)current_time;
            int dist = abs(time_diff * NOTE_SPEED);

            if (dist <= HIT_WINDOW) {
                if (keys_curr[n->lane] && !keys_prev[n->lane]) {
                    // Score Calculation
                    int hit_val = 10 - (dist / 2);
                    if (hit_val < 1) hit_val = 1;

                    current_score += hit_val;
                    spawn_effect(LANE_START_X + (n->lane * LANE_WIDTH), HIT_LINE_Y, hit_val);
                    n->timestamp = 0; // Mark hit
                }
            }
        }

        gfx_FillScreen(COL_WHITE);
        draw_ui();
        gfx_PrintStringXY("Score:", 10, 10);
        gfx_PrintInt(current_score, 0);
        gfx_PrintStringXY("High:", 10, 25);
        gfx_PrintInt(high_score, 0);

        for(int i = map_head; i < note_count; i++) {
            Note *n = &song_buffer[i];
            if (n->timestamp == 0) continue;

            int time_diff = (int)n->timestamp - (int)current_time;
            int y = HIT_LINE_Y - (time_diff * NOTE_SPEED);

            if (y < -20) break;
            if (y < 240) {
                gfx_TransparentSprite(note_sprite, LANE_START_X + (n->lane * LANE_WIDTH), y);
            }
        }
        
        draw_effects();
        memcpy(keys_prev, keys_curr, sizeof(keys_curr));
        gfx_SwapDraw();
        current_time++;

        if (map_head >= note_count) break;
    }

    // End Game - New Record Check
    bool new_record = false;
    if (current_score > high_score) {
        high_score = current_score;
        // Save ONLY the new high score, keeping the notes intact
        save_map(slot, high_score); 
        new_record = true;
    }

    while(1) {
        kb_Scan();
        if (kb_IsDown(KEY_EXIT)) {
            while(kb_IsDown(KEY_EXIT)) kb_Scan();
            break;
        }

        gfx_FillScreen(COL_WHITE);
        if(new_record) {
            gfx_SetTextFGColor(COL_GREEN);
            gfx_PrintStringXY("NEW HIGH SCORE!", 100, 80);
        } else {
            gfx_SetTextFGColor(COL_BLACK);
            gfx_PrintStringXY("SONG FINISHED", 110, 80);
        }
        
        gfx_SetTextFGColor(COL_BLACK);
        gfx_PrintStringXY("Final Score:", 115, 110);
        gfx_SetTextXY(140, 130);
        gfx_PrintInt(current_score, 0);
        gfx_PrintStringXY("Press Enter", 120, 180);
        gfx_SwapDraw();
    }
}

//! highscore 
void workflow_highscores() {
    while(1) {
        kb_Scan();
        if (kb_IsDown(KEY_EXIT)) {
            while(kb_IsDown(KEY_EXIT)) kb_Scan();
            break;
        }

        gfx_FillScreen(COL_WHITE);
        gfx_PrintStringXY("--- HIGH SCORES ---", 90, 40);

        for(int i=1; i<=4; i++) {
            gfx_SetTextXY(100, 70 + (i*25));
            gfx_PrintString("Slot "); 
            gfx_PrintInt(i, 0);
            gfx_PrintString(": ");
            
            int32_t score = peek_high_score(i);
            if(score == -1) gfx_PrintString("---");
            else gfx_PrintInt(score, 0);
        }

        gfx_PrintStringXY("Press Enter to Return", 90, 200);
        gfx_SwapDraw();
    }
}

// --- MAIN ---
int main(void) {
    gfx_Begin();
    gfx_SetDrawBuffer();
    init_graphics();

    while(1) {
        kb_Scan();
        gfx_FillScreen(COL_WHITE);
        
        gfx_SetTextXY(100, 40);
        gfx_PrintString("TI-RHYTHM 5-LANE");
        
        gfx_PrintStringXY("[1] Record Map", 80, 80);
        gfx_PrintStringXY("[2] Play Map", 80, 100);
        gfx_PrintStringXY("[3] High Scores", 80, 120);
        gfx_PrintStringXY("[Enter] Exit", 80, 160);
        
        gfx_SwapDraw();

        if (kb_IsDown(KEY_EXIT)) break;

        if (kb_IsDown(kb_Key1)) {
            while(kb_IsDown(kb_Key1)) kb_Scan();
            workflow_record();
        }

        if (kb_IsDown(kb_Key2)) {
            while(kb_IsDown(kb_Key2)) kb_Scan();
            workflow_play();
        }
        
        if (kb_IsDown(kb_Key3)) {
            while(kb_IsDown(kb_Key3)) kb_Scan();
            workflow_highscores();
        }
    }

    gfx_End();
    return 0;
}