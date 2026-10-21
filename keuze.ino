/*
MICROCONTROLLERS EINDPROJECT
Oskar Oostveen

4K mania game + joystick.

onderdelen:
- Arduino Uno
- 4 Push buttons
- 3 LEDs
- 128x64 Monochrome OLED
- 1 Joystick
- 1 Buzzer

Pins:
Pushbuttons: 8, 9, 10, 11 > GND
LEDs: 2, 3, 4 > GND
Display: SDA, SCK, GND, 5V > A4 A5
Joystick: A0, A1, 5V, GND
Buzzer: 6, GND

en als de joystick over de goede lane is. als dit niet zo is verlies je health en 50 punten.

De game wordt steeds sneller, het duurt maximaal rond do 55 seconden, daarna is het vrijwel onmogelijk.

Het doel is om een zo hoog mogelijke score en accuracy te krijgen.

*/

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>

Adafruit_SH1106G display = Adafruit_SH1106G(128, 64, &Wire, -1);

int beat[] = {120, 420, 380, 120};
uint8_t beat_index = 0;

/* 
note system
you have a array called lanes, lanes contains 4 lane, each lane containing 4 note_t.
in total there are 16 possible notes active at onces. 4 per lane. all are allocated at the start
so in total > (1 + 1) * 4 * 4 = 32 bytes for all notes. could be better :(

*/
uint8_t note_speed = 2;
const uint8_t note_width = 12;
const uint8_t note_height = 16;

/*
16 pixel padding from the ground. 
the button is 16 pixels tall, the note is another 16. 
the ground actually starts at 128 and goes downwards from there
THUS: 128-16 (bottom of the button) 128-32(top of the hitzone). -16 because the bottom of the note also counts.
in total there are 32 possible pixels to click.
IF note.pos_y IS WITHIN THIS RANGE WHEN THE BUTTON IS CLICKED IT COUNTS.
*/
const uint8_t hit_zone_min = 128-32-16;
const uint8_t hit_zone_max = 128-16;

struct note_t {
  bool active;
  uint8_t pos_y;
};
const uint8_t lane_amount = 4;
const uint8_t note_amount = 4;
note_t lanes[lane_amount][note_amount] = {0};

unsigned long game_start_time = millis();
uint16_t SPAWN_INTERVAL = 1200;
uint16_t spawn_time = 0;

// button physical wires are located at 8+++. you can change the start point but they have to be increasing.
const uint8_t button_start = 8;
bool button_states[4];
bool last_button_states[4];

uint16_t joystick_value;
uint8_t joystick_x;
uint8_t target_lane = 0;

bool in_game = false;
bool game_over = false;

/*
score system

+100 if a note is hit
+0-100 based on how accurate
+50 if lane is selected with joystick
*/

int32_t score = 0;
int32_t max_possible_score = 0;
uint8_t total_notes = 0;
uint8_t total_misses = 0;
float accuracy = 0.0f;
int8_t health = 100;
uint8_t difficulty = 1;

void setup() {
  pinMode(0, INPUT); // joystick y
	pinMode(1, INPUT); // joystick x

  pinMode(6, OUTPUT); //buzzer

  //leds
  pinMode(2, OUTPUT);
  pinMode(3, OUTPUT);
  pinMode(4, OUTPUT);
  digitalWrite(2, HIGH);
  digitalWrite(3, HIGH);
  digitalWrite(4, HIGH);

  //buttons
  pinMode(8, INPUT_PULLUP);
  pinMode(9, INPUT_PULLUP);
  pinMode(10, INPUT_PULLUP);
  pinMode(11, INPUT_PULLUP);

  display.begin(0x3C, true); 
  display.setRotation(1);
  display.clearDisplay();

  Serial.begin(9600);

  lanes[0][0].active = true;
}

unsigned long loop_start_time = 0; 
const unsigned long LOOP_PERIOD = 40;

// will be the audio manager
void playSound(int frequency, int duration) {
  tone(6, frequency, duration);
}

void updateNotes() {
  joystick_value = analogRead(1);
  joystick_x = map(joystick_value, 0, 1024, 0, 64);
  target_lane = joystick_x/16;

  unsigned long match_elapsed_time = millis() - game_start_time;

  if(match_elapsed_time > 5000) {
    difficulty = 2;
    note_speed = 3;
    SPAWN_INTERVAL = 800;
  }
  if(match_elapsed_time > 12500) {
    difficulty = 3;
    note_speed = 4;
    SPAWN_INTERVAL = 500;
  }
  if(match_elapsed_time > 20000) {
    difficulty = 4;
    note_speed = 5;
    SPAWN_INTERVAL = 400;
  }
  if(match_elapsed_time > 30000) {
    difficulty = 5;
    note_speed = 6;
    SPAWN_INTERVAL = 300;
  }
  if(match_elapsed_time > 40000) {
    difficulty = 6;
    note_speed = 8;
    SPAWN_INTERVAL = 180;
  }





  //spawn new note if interval. plus changing all note speeds.
  if(spawn_time >= SPAWN_INTERVAL) {
    playSound(beat[beat_index], 150);
    beat_index++;
    if(beat_index >= 4) { beat_index = 0; }
    spawn_time = 0;
    uint8_t random_lane = random(4);
    for(uint8_t note_i = 0; note_i < note_amount; note_i++) {
      note_t &note = lanes[random_lane][note_i]; //refrence the current note
      if(!note.active){ 
        note.active = true;
        note.pos_y = 0;
        break;
      }
    }
  }

  //move active notes down + check for pressed
  for(uint8_t lane_i = 0; lane_i < lane_amount; lane_i++) { //go trough each lane
    bool button_pressed = !button_states[lane_i] && last_button_states[lane_i];
    bool note_hit = false;
    for(uint8_t note_i = 0; note_i < note_amount; note_i++) { //in each lane go trough every note
      note_t &note = lanes[lane_i][note_i]; //refrence the current note
      if(note.active) {
        //check for pressed. first check if they are in the zone. they click if the current button state is down and the previous one is up. since it should only fire on pressed not hold.
        if(button_pressed) {
          if(note.pos_y >= hit_zone_min && note.pos_y <= hit_zone_max) {
            note_hit = true;
            //0 is perfect hit, 16 is furthest possible hit (aka bad).
            uint8_t distance = abs(96-note.pos_y);
            uint8_t hit_acc = 100-((distance*100) + 8)/16;
            score += 100+hit_acc;
            if(target_lane == lane_i) {
              score += 50;
              health += 5;
            }
            else {
              score -= 50;
              health -= 5;
            }
            //calc new accuracy
            max_possible_score += 250;
            accuracy = ((float)score / (float)max_possible_score) * 100.0f;
            total_notes++;
            note.active = false;
          }
        }

        //move the note down if its active
        note.pos_y += note_speed;
        if(note.pos_y >= 128) { //if button is off screen
          health -= 15;
          total_notes++;
          total_misses++;
          //calc new accuracy
          max_possible_score += 250;
          accuracy = ((float)score / (float)max_possible_score) * 100.0f;
          note.active = false;
        }
      }
    }
    if(!note_hit && button_pressed) { score -= 200; health -= 15; }
  }
}

//draw everything to screen
void drawScene() {
  display.clearDisplay();
  
  //draw the buttons
  for(uint8_t i = 0; i < 4; i++) {
    if (button_states[i] == LOW) { display.fillRect(2+16*i, 128-32, note_width, note_height, 1); } // if button pressed draw filled rect
    else { display.drawRect(2+16*i, 128-32, note_width, note_height, 1); } // else draw only outline
  }

  //draw_notes (check update notes for explanation):
  uint8_t note_x = 0;
  for(uint8_t lane_i = 0; lane_i < lane_amount; lane_i++) { 
    for(uint8_t note_i = 0; note_i < note_amount; note_i++) {
        note_t &note = lanes[lane_i][note_i];
        if(note.active) {
          note_x = 16*lane_i + 2;
          display.fillRect(note_x, note.pos_y, note_width, note_height, 1);
          display.drawRect(note_x, note.pos_y, note_width, note_height, 0);
        }
    }
  }
  display.drawFastVLine(target_lane*16+8, 0, 128, 1);

  //debug text
  // display.setTextSize(1);
  // display.setTextColor(SH110X_WHITE);
  // display.setCursor(70, 0);
  // display.print("sc:");
  // display.print(score);
  // display.setCursor(70, 10);
  // display.print("<3:");
  // display.print(health);
  // display.setCursor(70, 20);
  // display.print("acc:");
  // display.print(accuracy);
  // display.setCursor(70, 30);
  // display.print("diff:");
  // display.print(difficulty);
  display.display();
}

void mainMenu() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(8, 32);
  display.print("4K-MANIA");

  display.drawRect(4, 48, 56, 16, 1);
  display.setCursor(16, 52);
  display.print("START");

  if(digitalRead(8) == LOW) { in_game = true; }

  display.display();
}

void gameOver() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(4, 32);
  display.print("Game Over!");

  //display.drawRect(4, 48, 56, 16, 1);
  display.setCursor(4, 52);
  display.print("SCORE:");
  display.print(score);
  display.setCursor(4, 64);
  display.print("ACC%:");
  display.print(accuracy);
  display.setCursor(4, 78);
  display.print("NOTES:");
  display.print(total_notes);
  display.setCursor(4, 90);
  display.print("MISS:");
  display.print(total_misses);

  display.drawRect(4, 100, 56, 16, 1);
  display.setCursor(12, 104);
  display.print("RESTART");

  if(!button_states[0] && last_button_states[0]) { 
    in_game = true;
    game_over = false;

    score = 0;
    max_possible_score = 0;
    total_notes = 0;
    total_misses = 0;
    accuracy = 0.0f;
    health = 100;
    difficulty = 1;
    note_speed = 2;

    game_start_time = millis();
    SPAWN_INTERVAL = 1200;
    spawn_time = 0;

    for(uint8_t l = 0; l < lane_amount; l++) {
      for(uint8_t n = 0; n < note_amount; n++) {
        lanes[l][n].active = false;
        lanes[l][n].pos_y = 0;
      }
    }
  }

  display.display();
}

void loop() {
  loop_start_time = millis();

  for(uint8_t i=0;i<4;i++) {
    button_states[i] = digitalRead(button_start+i);
  }

  if(in_game) {
    if(health >= 1) {
      updateNotes();
      drawScene();
    }

    if(health >= 100) { health = 100; digitalWrite(4, HIGH); digitalWrite(3, HIGH); digitalWrite(2, HIGH);}
    else if(health >= 66) { digitalWrite(4, HIGH); digitalWrite(3, HIGH); digitalWrite(2, HIGH);}
    else if(health >= 33) { digitalWrite(4, HIGH); digitalWrite(3, HIGH); digitalWrite(2, LOW); }
    else if(health >= 1 ) { digitalWrite(4, HIGH); digitalWrite(3, LOW); digitalWrite(2, LOW); }
    else { digitalWrite(4, LOW); digitalWrite(3, LOW); digitalWrite(2, LOW); }

    if(health <= 0) { game_over = true; in_game = false; playSound(20, 2000); delay(2000);}

    spawn_time += LOOP_PERIOD;
  } 
  else {
    if(!game_over) {
      mainMenu();
    }
    else {
      gameOver();
    }
    
  }

  //set last_button_states to current_button
  memcpy(last_button_states, button_states, sizeof(last_button_states));

  while (millis() - loop_start_time < LOOP_PERIOD) {}
}
