/***********************************************************************************
  This program was made for LCD modules with 8bit data port.
  This program requires the the LCDKIWI library.

  File                : GUESSFrase.ino
  Hardware Environment: Arduino MEGA
  Build Environment   : Arduino

  Set the pins to the correct ones for your development shield or breakout board.
  This demo use the BREAKOUT BOARD only and use these 8bit data lines to the LCD,
  pin usage as follow:
                   LCD_CS  LCD_CD  LCD_WR  LCD_RD  LCD_RST  SD_SS  SD_DI  SD_DO  SD_SCK
      Arduino Uno    A3      A2      A1      A0      A4      10     11     12      13
 Arduino Mega2560    A3      A2      A1      A0      A4      53     51     50      52
                   LCD_D0  LCD_D1  LCD_D2  LCD_D3  LCD_D4  LCD_D5  LCD_D6  LCD_D7
       Arduino Uno    8       9       2       3       4       5       6       7
  Arduino Mega2560    8       9       2       3       4       5       6       7 

  Remember to set the pins to suit your display module!
**********************************************************************************/
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <TouchScreen.h> //Touch library
#include <LCDWIKI_GUI.h> //Core graphics library
#include <LCDWIKI_KBV.h> //Hardware-specific library

LCDWIKI_KBV myLcd(ILI9486, A3, A2, A1, A0, A4); //model,cs,cd,wr,rd,reset
// * * Unused Pins: A5, D0(->Rx), D1(->Tx) * * //

// Set SD Card Chip Select (CS) pin
#define MEGA 1
#if MEGA
  #define CS_PIN 53    //Mega/Due only
#else
  #define CS_PIN 10    //Uno only
#endif

#define TOUCH_ORIENTATION  1  //0=Portrait, 1=Landscape

//// COLOR DEFINITION STUFF ////
#define BLACK   0x0000
#define BLUE    0x001F
#define RED     0xF800
#define GREEN   0x07E0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define YELLOW  0xFFE0
#define WHITE   0xFFFF

//// SCREEN SETUP STUFF ////
#define YP A3  // must be an analog pin, use "An" notation!
#define XM A2  // must be an analog pin, use "An" notation!
#define YM 9   // can be a digital pin
#define XP 8   // can be a digital pin
TouchScreen ts = TouchScreen(XP, YP, XM, YM, 292);
#define TS_MINX 952
#define TS_MAXX 92
#define TS_MINY 906
#define TS_MAXY 116

#define MINPRESSURE 10
#define MAXPRESSURE 1000

int dispx, dispy, y_center, x_center, numLines;
const int maxPhraseLineLength_SIZE6 = 13;
const int maxPhraseLineLength_SIZE4 = 20;
const int maxPhraseLineLength_SIZE3 = 26;
const int maxPhraseLineLength_SIZE2 = 40;
char currentPage;
String currentCategory;
File phraseFile;

//// BUTTON STUFF ////
const int passButtonPin = 45;
int passButtonState = 0;

//// TONE STUFF ////
const int tonePin = 47;
unsigned long toneDuration;
unsigned int  toneFrequency;
const unsigned int hapticFeedbackToneDuration = 45;
const unsigned int hapticFeedbackToneFrequency = 200;
volatile bool outputTone = false;                // Records current state

//// TIMER STUFF ////
unsigned long previousMillis1, previousMillis2;            // will store last time timer was updated
unsigned long interval1 = 1000;          //
unsigned long minGameTime = 90;          // minimum time for game play (seconds)
unsigned long maxGameTime = 180;         // maximum time for game play (seconds)
unsigned long timeRemaining;

unsigned long gameTime;
unsigned long gameTimeRemaining;
volatile bool gameInPlay = false;

//// SHAKE SENSOR STUFF ////
const int shakePin = 42;
bool lastShakeState = false;
int shakeCounter = 0;
unsigned long shakeTime = 600;
unsigned long lastShakeTime;

//// Vibration motor stuff ////
const int motorPin = 42;

/**
 * Function printDirectory prints file data to serial monitor.
 * @param file [File] File of interest
 * @param numTabs [int] How many tabs to print
 */
void printDirectory(File dir, int numTabs)
{
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) { // no more files
      break;
    }
    for (uint8_t i = 0; i < numTabs; i++) {
      Serial.print('\t');
    }
    Serial.print(entry.name());
    if (entry.isDirectory()) {
      Serial.println("/");
      printDirectory(entry, numTabs + 1);
    }
    else { // files have sizes, directories do not
      Serial.print("\t\t");
      Serial.println(entry.size(), DEC);
    }
    entry.close();
  }
}
/**
 * Function is_pressed checks to see if the TFT screen has been pressed or not
 * @param x1 [int16_t] x1 screen coordinate
 * @param y1 [int16_t] y1 screen coordinate
 * @param x2 [int16_t] x2 screen coordinate
 * @param y2 [int16_t] y2 screen coordinate
 * @param px [int16_t] px pressure at x
 * @param py [int16_t] py pressure at y
 */
boolean is_pressed(int16_t x1, int16_t y1, int16_t x2, int16_t y2, int16_t px, int16_t py)
{
  if ((px > x1 && px < x2) && (py > y1 && py < y2)) {
    return true;
  } else { return false; }
}

/**
 * Function getPassButtonState checks to see if the physical button has been pressed
 */
boolean getPassButtonState()
{
  passButtonState = digitalRead(passButtonPin);
  if (passButtonState == HIGH){
    //Serial.print("passButtonState: "); Serial.println(passButtonState);
    return true;
  } else {
    //Serial.print("passButtonState: "); Serial.println(passButtonState);
    return false; }
}

/**
 * Function getShakeState checks to see user has shaked device to change word
 */
boolean getShakeState()
{
  // Sense shake & increment counter
  if(lastShakeState != digitalRead(shakePin)){
    shakeCounter++;
    lastShakeState = !lastShakeState;
    // Serial.print("+1 shake\n");
    delay(25);
  }
  // Alert that it has been shaken hard enough
  if(shakeCounter == 4){
    // Serial.print("Shaked!\n");
    shakeCounter = 0;
    return true;
  }
  // Reset counter for shake timing
  if(millis() - lastShakeTime > shakeTime){
    //Serial.print("Shake timer up\n");
    shakeCounter = 0;
    lastShakeTime = millis();
  }
  lastShakeState = digitalRead(shakePin);

  return false;
}

/**
 * Function getNumberOfLines checks open file to see how many entries are present.
 * This will be used for random number limits
 */
int getNumberOfLines()
{
  int numLines = 0;
  phraseFile.seek(0);
  char cr;

  while (phraseFile.available()) {
    cr = phraseFile.read();
    if(cr == '\n' || cr == '\t'){
      numLines++;
    }
  }
  return numLines;
}

/**
 * Function openPhrasaeFile opens file on SD card
 * @param fileName [string] file in which to open
 */
void openPhraseFile(String fileName)
{ 
  fileName += ".txt";
  char fileNameChar [fileName.length()];
  fileName.toCharArray(fileNameChar,fileName.length());

  phraseFile = SD.open(fileName, FILE_READ);    // open the file
  if (phraseFile) {
    Serial.print(fileName);
    Serial.println(" is open.");
    Serial.print("Number of lines: ");
    numLines = getNumberOfLines();
    Serial.println(numLines);
  } else {    // if the file didn't open, print an error:
    Serial.print("Error opening ");
    Serial.println(fileName);
  }
}

/**
 * Function openPhrasaeFile opens file on SD card
 * @param lineNumber [unsigned int] the line from random seed to populate screen with
 */
String getPhrase(unsigned int lineNumber)
{
  String phrase;
  phraseFile.seek(0);
  char cr;
  
  for(unsigned int i = 0; i < (lineNumber - 1);){   // Find line number
    cr = phraseFile.read();
    if(cr == '\n'){
      i++;
    }
  }
  
  while(phraseFile){            // Store line as string
    cr = phraseFile.read();
    phrase += cr;
    if(cr == '\n' || cr == '\t'){
      break;
    }
  }
  phrase.trim();
  return phrase;
}

/**
 * Function drawInitializationPage draws predefined page to indicate start up
 */
void drawInitializationPage(String initializationStatus)
{
  myLcd.Fill_Screen(BLACK);
  myLcd.Set_Text_Back_colour(BLACK);
  myLcd.Set_Text_colour(WHITE);
  myLcd.Set_Text_Size(2);
  myLcd.Print_String(initializationStatus, CENTER, 150);
}

/**
 * Function drawHomePage draws predefined home page
 */
void drawHomePage()
{
  myLcd.Fill_Screen(BLACK);
  myLcd.Set_Text_Back_colour(BLACK);
  myLcd.Set_Text_colour(WHITE);
  myLcd.Set_Text_Size(6);
  myLcd.Print_String("GUESS FRASE", CENTER, 3);
  myLcd.Set_Draw_color(RED);
  myLcd.Fill_Round_Rectangle(x_center - 150, 90, x_center + 150, 150, 5);  //Categories box x1, y1, x2, y2
  myLcd.Fill_Round_Rectangle(x_center - 150, 200, x_center + 150, 260, 5); //Settings box
  myLcd.Set_Draw_color(WHITE);
  myLcd.Fill_Round_Rectangle(x_center - 147, 93, x_center + 147, 147, 5);
  myLcd.Fill_Round_Rectangle(x_center - 147, 203, x_center + 147, 257, 5);
  myLcd.Set_Text_colour(BLACK);
  myLcd.Set_Text_Back_colour(WHITE);
  myLcd.Set_Text_Size(4);
  myLcd.Print_String("Categories", CENTER, 105);
  myLcd.Print_String("About", CENTER, 215);
}

/**
 * Function drawAboutPage draws predefined creditation page
 */
void drawAboutPage()
{
  Serial.print("Current page: "); Serial.println(currentPage);
  myLcd.Fill_Screen(BLACK);
  myLcd.Set_Text_Back_colour(BLACK);
  myLcd.Set_Text_colour(WHITE);
  myLcd.Set_Text_Size(6);
  myLcd.Print_String("GUESS FRASE", CENTER, 3);
  myLcd.Set_Text_Size(4);
  myLcd.Print_String("Created by:", CENTER, 70);
  myLcd.Set_Text_colour(RED);
  myLcd.Print_String("CR & AW", CENTER, 110);
  myLcd.Set_Text_colour(WHITE);
  myLcd.Set_Text_Size(2);
  myLcd.Print_String(" Programmed using Visual Code Studio", LEFT, 155);
  myLcd.Print_String(" and PlatformIO", LEFT, 175);
  myLcd.Set_Text_Size(1);
  myLcd.Print_String("   Parts used: Arduino Mega", LEFT, 205);
  myLcd.Print_String("               kuman 3.5 inch TFT Touch Screen with SD Card Socket", LEFT, 215);
  myLcd.Print_String("               kLANMU Micro SD to SD Card Extension Cable", LEFT, 225);
  myLcd.Print_String("               Diymore Double 18650 V8 Lithium Battery Shield", LEFT, 235);
  myLcd.Print_String("               GFORTUN Active Piezo Buzzer", LEFT, 245);
  myLcd.Print_String("               DAOKI 6x6x5 mm Micro Momentary Tactile Push Button Switch", LEFT, 255);
  myLcd.Set_Text_Size(2);
  myLcd.Print_String(" Printed parts designed in SOLIDWORKS", LEFT, 275);
  myLcd.Print_String("        and printed using PLA", LEFT, 295);

}

/**
 * Function drawCategoriesPage draws predefined category selection page
 */
void drawCategoriesPage()
{
  Serial.print("Current page: "); Serial.println(currentPage);
  myLcd.Fill_Screen(BLACK);
  myLcd.Set_Text_Back_colour(BLACK);
  myLcd.Set_Text_colour(WHITE);
  myLcd.Set_Text_Size(6);
  myLcd.Print_String("GUESS FRASE", CENTER, 3);
  myLcd.Set_Draw_color(RED);
  myLcd.Fill_Round_Rectangle(x_center / 2 - 115, 60,  x_center / 2 + 35,  120, 5); //Everything box
  myLcd.Fill_Round_Rectangle(x_center - 75,      60,  x_center + 75,      120, 5); //Math/Science box
  myLcd.Fill_Round_Rectangle(x_center / 2 + 205, 60,  x_center / 2 + 355, 120, 5); //Adult box
  myLcd.Fill_Round_Rectangle(x_center / 2 - 115, 130, x_center / 2 + 35,  190, 5); //Entertainment box
  myLcd.Fill_Round_Rectangle(x_center - 75,      130, x_center + 75,      190, 5); //Sports box
  myLcd.Fill_Round_Rectangle(x_center / 2 + 205, 130, x_center / 2 + 355, 190, 5); //History box
  myLcd.Fill_Round_Rectangle(x_center / 2 - 115, 200, x_center / 2 + 35,  260, 5); //Household box
  myLcd.Fill_Round_Rectangle(x_center - 75,      200, x_center + 75,      260, 5); //Plants/Animals box
  myLcd.Fill_Round_Rectangle(x_center / 2 + 205, 200, x_center / 2 + 355, 260, 5); //Technology box
  myLcd.Fill_Round_Rectangle(x_center / 2 + 35,  270, x_center / 2 + 205, 310, 5); //Back box
  myLcd.Set_Draw_color(WHITE);
  myLcd.Fill_Round_Rectangle(x_center / 2 - 112, 63,  x_center / 2 + 32,  117, 5); //Everything box
  myLcd.Fill_Round_Rectangle(x_center - 72,      63,  x_center + 72,      117, 5); //Math/Science box
  myLcd.Fill_Round_Rectangle(x_center / 2 + 208, 63,  x_center / 2 + 352, 117, 5); //Adult box
  myLcd.Fill_Round_Rectangle(x_center / 2 - 112, 133, x_center / 2 + 32,  187, 5); //Entertainment box
  myLcd.Fill_Round_Rectangle(x_center - 72,      133, x_center + 72,      187, 5); //Sports box
  myLcd.Fill_Round_Rectangle(x_center / 2 + 208, 133, x_center / 2 + 352, 187, 5); //History box
  myLcd.Fill_Round_Rectangle(x_center / 2 - 112, 203, x_center / 2 + 32,  257, 5); //Household box
  myLcd.Fill_Round_Rectangle(x_center - 72,      203, x_center + 72,      257, 5); //Plants/Animals box
  myLcd.Fill_Round_Rectangle(x_center / 2 + 208, 203, x_center / 2 + 353, 257, 5); //Technology box
  myLcd.Fill_Round_Rectangle(x_center / 2 + 38,  273, x_center / 2 + 202, 307, 5); //Back box
  myLcd.Set_Text_colour(BLACK);
  myLcd.Set_Text_Back_colour(WHITE);
  myLcd.Set_Text_Size(2);
  myLcd.Print_String("Everything",   x_center / 2 - 100, 82);
  myLcd.Print_String("Math/Science", x_center - 71,      82);
  myLcd.Print_String("Adult",        x_center / 2 + 250, 82);
  myLcd.Print_String("Entertainmnt", x_center / 2 - 111, 153);
  myLcd.Print_String("Sports",       x_center - 35,      153);
  myLcd.Print_String("History",      x_center / 2 + 240, 153);
  myLcd.Print_String("Household",    x_center / 2 - 96,  223);
  myLcd.Print_String("Plant/Animal", x_center - 71,      223);
  myLcd.Print_String("Technology",   x_center / 2 + 220, 223);
  myLcd.Set_Text_Size(3);
  myLcd.Print_String("Back",         x_center / 2 + 85,  280);
}

/**
 * Function drawStartPage draws predefined page indicating category selection,
 * ready for player to start game
 * @param category [string] selected category
 */
void drawStartPage(String category)
{
  Serial.print("Current page: "); Serial.println(currentPage);
  myLcd.Fill_Screen(BLACK);
  myLcd.Set_Text_Back_colour(BLACK);
  myLcd.Set_Text_colour(WHITE);
  myLcd.Set_Text_Size(6);
  myLcd.Print_String("GUESS FRASE", CENTER, 3);
  myLcd.Set_Draw_color(RED);
  myLcd.Fill_Round_Rectangle(x_center - 165,    180, x_center + 150,     250, 5);  //Start box
  myLcd.Fill_Round_Rectangle(x_center / 2 + 35, 270, x_center / 2 + 205, 310, 5); //Back box
  myLcd.Set_Draw_color(WHITE);
  myLcd.Fill_Round_Rectangle(x_center - 162,    183, x_center + 147,     247, 5);  //Start box
  myLcd.Fill_Round_Rectangle(x_center / 2 + 38, 273, x_center / 2 + 202, 307, 5); //Back box
  myLcd.Set_Text_colour(WHITE);
  myLcd.Set_Text_Back_colour(RED);
  myLcd.Set_Text_Size(6);
  myLcd.Print_String(category, CENTER, 85);
  myLcd.Set_Text_colour(BLACK);
  myLcd.Set_Text_Back_colour(WHITE);
  myLcd.Set_Text_Size(6);
  myLcd.Print_String("Start", CENTER, 195);
  myLcd.Set_Text_Size(3);
  myLcd.Print_String("Back",  CENTER, 280);
}

/**
 * Function drawPhrasePage draws predefined page during gameplay
 */
void drawPhrasePage()
{
  Serial.print("Current page: "); Serial.println(currentPage);
  myLcd.Fill_Screen(BLACK);
  myLcd.Set_Text_Back_colour(BLACK);
  myLcd.Set_Text_colour(WHITE);
  myLcd.Set_Text_Size(6);
  myLcd.Print_String("GUESS FRASE", CENTER, 3);
  myLcd.Set_Draw_color(RED);
  myLcd.Fill_Round_Rectangle(x_center - 165,    180, x_center + 150,     250, 5);  //Next box
  myLcd.Fill_Round_Rectangle(x_center / 2 + 35, 270, x_center / 2 + 205, 310, 5); //Back box
  myLcd.Set_Draw_color(WHITE);
  myLcd.Fill_Round_Rectangle(x_center - 162,    183, x_center + 147,     247, 5);  //Next box
  myLcd.Fill_Round_Rectangle(x_center / 2 + 38, 273, x_center / 2 + 202, 307, 5); //Back box
  myLcd.Set_Text_colour(BLACK);
  myLcd.Set_Text_Back_colour(WHITE);
  myLcd.Print_String("Pass", CENTER, 195);
  myLcd.Set_Text_Size(3);
  myLcd.Print_String("End Game", CENTER, 280);
}

/**
 * Function drawPhrase draws the phrase to be described
 * @param phrase [String] Randomly selected phrase to be described
 */
void drawPhrase(String phrase)
{
  myLcd.Set_Text_colour(YELLOW);
  myLcd.Set_Text_Back_colour(BLACK);
  myLcd.Set_Text_Size(6);
  myLcd.Print_String("                      ", CENTER, 75);
  myLcd.Print_String("                      ", CENTER, 115);

  if (phrase.length() <= maxPhraseLineLength_SIZE6){
    myLcd.Set_Text_Size(6);

  } else if (phrase.length() > maxPhraseLineLength_SIZE6 && phrase.length() <= maxPhraseLineLength_SIZE4) {
    myLcd.Set_Text_Size(4);
  } else if (phrase.length() > maxPhraseLineLength_SIZE4 && phrase.length() <= maxPhraseLineLength_SIZE3) {
    myLcd.Set_Text_Size(3);
  } else if (phrase.length() > maxPhraseLineLength_SIZE3 && phrase.length() <= maxPhraseLineLength_SIZE2) {
    myLcd.Set_Text_Size(2);
  } else {
    drawPhrase(getPhrase(random(1,numLines)));    //If longer than maxPhraseLineLength_SIZE2, get a new phrase.
  }
    myLcd.Print_String(phrase, CENTER, 95);
    Serial.println(phrase);
}

/**
 * Function drawGameOverPage draws the predefined game over page once timer has ran out
 */
void drawGameOverPage()
{
  Serial.print("Current page: "); Serial.println(currentPage);
  myLcd.Fill_Screen(BLACK);
  myLcd.Set_Text_Back_colour(BLACK);
  myLcd.Set_Text_colour(WHITE);
  myLcd.Set_Text_Size(6);
  myLcd.Print_String("GUESS FRASE", CENTER, 3);
  myLcd.Set_Text_colour(RED);
  myLcd.Set_Text_Size(8);
  myLcd.Print_String("GAME OVER", CENTER, 110);
  myLcd.Set_Draw_color(RED);
  myLcd.Fill_Round_Rectangle(x_center / 2 - 115, 250, x_center / 2 + 35,  310, 5); //Replay box
  myLcd.Fill_Round_Rectangle(x_center - 75,      250, x_center + 75,      310, 5); //Categories box
  myLcd.Fill_Round_Rectangle(x_center / 2 + 205, 250, x_center / 2 + 355, 310, 5); //Home box
  myLcd.Set_Draw_color(WHITE);
  myLcd.Fill_Round_Rectangle(x_center / 2 - 112, 253, x_center / 2 + 32,  307, 5); //Replay box
  myLcd.Fill_Round_Rectangle(x_center - 72,      253, x_center + 72,      307, 5); //Categories box
  myLcd.Fill_Round_Rectangle(x_center / 2 + 208, 253, x_center / 2 + 353, 307, 5); //Home box
  myLcd.Set_Text_colour(BLACK);
  myLcd.Set_Text_Back_colour(WHITE);
  myLcd.Set_Text_Size(2);
  myLcd.Print_String("Replay",     x_center / 2 - 75,  273);
  myLcd.Print_String("Categories", CENTER,             273);
  myLcd.Print_String("Home",       x_center / 2 + 260, 273);
}

/**
 * Function setup initalizes Arduino for operation
 */
void setup()
{
 initialize:
  Serial.begin(9600);
  myLcd.Init_LCD(); //initialize lcd
  myLcd.Set_Rotation(TOUCH_ORIENTATION);
  Serial.print("Initializing SD card...");
  drawInitializationPage("Initializing SD card...");
  pinMode(CS_PIN, OUTPUT);
    SD.begin(CS_PIN);
  if (!SD.begin(CS_PIN)) {
    Serial.println("SD Card initialization failed!");
    drawInitializationPage("SD Card initialization failed!");
    delay(2000);
    goto initialize;
  }
  Serial.println("SD card initialization done.");
  drawInitializationPage("SD card initialization done.");
  delay(500);

  // File root;              //Print file list
  // root = SD.open("/");
  // printDirectory(root, 0);

  //Serial.println(myLcd.Read_ID(), HEX);
  myLcd.Fill_Screen(BLACK);
  digitalWrite(A0, HIGH);
  pinMode(A0, OUTPUT);
  pinMode(13, OUTPUT);
  pinMode(passButtonPin, INPUT);  //Physical pass button
  pinMode(shakePin,INPUT);  //Shake sensor
  lastShakeState = digitalRead(shakePin); //get current state of shakePin
  lastShakeTime = millis();
  pinMode(motorPin,OUTPUT);
  digitalWrite(motorPin,LOW); // make sure vibration motor is off
  randomSeed(analogRead(A5));
  dispx = myLcd.Get_Display_Width();
  dispy = myLcd.Get_Display_Height();
  //Serial.print("X_max = "); Serial.println(dispx);
  //Serial.print("Y_max = "); Serial.println(dispy);
  x_center = dispx / 2;
  y_center = dispy / 2;
  drawHomePage();    // Draws the Home Screen
  currentPage = '0'; // Indicates that we are at Home Screen
}

/**
 * Function loop is the main loop of the program
 */
void loop()
{
 base:
  digitalWrite(13, HIGH);
  TSPoint p = ts.getPoint();  // A point object holds x y and z coordinates
  digitalWrite(13, LOW);
  pinMode(XM, OUTPUT);
  pinMode(YP, OUTPUT);

  if ( (getPassButtonState() || getShakeState() ) && currentPage == '3') { // Check if physical button pressed or shake sensed
    tone(tonePin, hapticFeedbackToneFrequency, hapticFeedbackToneDuration);
    drawPhrase(getPhrase(random(1,numLines)));
    Serial.println("Button pressed");
    goto base;
  }

  if (p.z > MINPRESSURE && p.z < MAXPRESSURE) {
    p.x = map(p.x, TS_MINX, TS_MAXX, myLcd.Get_Display_Width(), 0);
    p.y = map(p.y, TS_MINY, TS_MAXY, myLcd.Get_Display_Height(), 0);
    Serial.print("X = "); Serial.print(p.x);
    Serial.print("\tY = "); Serial.print(p.y);
    Serial.print("\tPressure = "); Serial.println(p.z);

    // Home Screen
    if (currentPage == '0') {
      if (is_pressed(260, 55, 330, 275, p.x, p.y)) { //x1, y1, x2, y2   // Categories selected
        tone(tonePin, hapticFeedbackToneFrequency, hapticFeedbackToneDuration);
        currentPage = '1';
        drawCategoriesPage();
      }
      if (is_pressed(105, 55, 175, 275, p.x, p.y)) {    // About selected
        tone(tonePin, hapticFeedbackToneFrequency, hapticFeedbackToneDuration);
        currentPage = '5';
        drawAboutPage();
        delay(6000);  // Show for 6 seconds
        drawHomePage();
        currentPage = '0';
      }
      goto base;
    }

    // Categoties Page
    if (currentPage == '1') {
      if (is_pressed(308, 235, 380, 335, p.x, p.y)) {   //Everything sleected
        tone(tonePin, hapticFeedbackToneFrequency, hapticFeedbackToneDuration);
        currentPage = '2';
        currentCategory = "Everything";
        drawStartPage(currentCategory);
      }
      if (is_pressed(305, 115, 375, 220, p.x, p.y)) {   //Math/Science selected
        tone(tonePin, hapticFeedbackToneFrequency, hapticFeedbackToneDuration);
        currentPage = '2';
        currentCategory = "Math/Science";
        drawStartPage(currentCategory);
      }
      if (is_pressed(300, -5, 375, 100, p.x, p.y)) {    //Adult selected
        tone(tonePin, hapticFeedbackToneFrequency, hapticFeedbackToneDuration);
        currentPage = '2';
        currentCategory = "Adult";
        drawStartPage(currentCategory);
      }
      if (is_pressed(200, 233, 273, 333, p.x, p.y)) {   //Entertainment selected
        tone(tonePin, hapticFeedbackToneFrequency, hapticFeedbackToneDuration);
        currentPage = '2';
        currentCategory = "Entertainment";
        drawStartPage(currentCategory);
      }
      if (is_pressed(200, 115, 275, 218, p.x, p.y)) {   //Sports selected
        tone(tonePin, hapticFeedbackToneFrequency, hapticFeedbackToneDuration);
        currentPage = '2';
        currentCategory = "Sports";
        drawStartPage(currentCategory);
      }
      if (is_pressed(200, -5, 275, 100, p.x, p.y)) {    // History selected
        tone(tonePin, hapticFeedbackToneFrequency, hapticFeedbackToneDuration);
        currentPage = '2';
        currentCategory = "History";
        drawStartPage(currentCategory);
      }
      if (is_pressed(105, 230, 175, 335, p.x, p.y)) {   // Household selected
        tone(tonePin, hapticFeedbackToneFrequency, hapticFeedbackToneDuration);
        currentPage = '2';
        currentCategory = "Household";
        drawStartPage(currentCategory);
      }
      if (is_pressed(105, 110, 175, 215, p.x, p.y)) {   //Plant/Animal selected
        tone(tonePin, hapticFeedbackToneFrequency, hapticFeedbackToneDuration);
        currentPage = '2';
        currentCategory = "Plant/Animal";
        drawStartPage(currentCategory);
      }
      if (is_pressed(105, -5, 170, 100, p.x, p.y)) {    // Technology selected
        tone(tonePin, hapticFeedbackToneFrequency, hapticFeedbackToneDuration);
        currentPage = '2';
        currentCategory = "Technology";
        drawStartPage(currentCategory);
      }
      if (is_pressed(30, 105, 80, 225, p.x, p.y)) {   // Back selected
        tone(tonePin, hapticFeedbackToneFrequency, hapticFeedbackToneDuration);
        currentPage = '0';
        drawHomePage();
      }
      if (currentCategory == "Math/Science") {      //Correct for FAT32 file naming convention
        currentCategory = "Science";
      } else if (currentCategory == "Plant/Animal") {
        currentCategory = "Plant";
      }

      currentCategory.remove(7);
      openPhraseFile(currentCategory);

      goto base;
    }

    // Start Game Page
    if (currentPage == '2') {
      toneDuration  = 300;
      toneFrequency = 880;
      if (is_pressed(115, 55, 200, 285, p.x, p.y)) {  // Start selected
        tone(tonePin, hapticFeedbackToneFrequency, hapticFeedbackToneDuration);
        currentPage = '3';
        drawPhrasePage();
        drawPhrase(getPhrase(random(1,numLines)));
        gameTime = random(minGameTime, maxGameTime);
        gameTimeRemaining = gameTime;
        previousMillis1 = millis();
        Serial.print("Game time: ");
        Serial.print(gameTime);
        Serial.println(" seconds");
        gameInPlay = true;
        goto gametimer;
      }
      if (is_pressed(30, 105, 80, 225, p.x, p.y)) {   // Back selected
        tone(tonePin, hapticFeedbackToneFrequency, hapticFeedbackToneDuration);
        currentPage = '1';
        drawCategoriesPage();
        phraseFile.close();
      }
      goto base;
    }

    // Game In Progress Page
    if (currentPage == '3') {
      if (is_pressed(115, 55, 200, 285, p.x, p.y)) {   // Pass selected
        tone(tonePin, hapticFeedbackToneFrequency, hapticFeedbackToneDuration);
        drawPhrase(getPhrase(random(1,numLines)));
      }
      if (is_pressed(30, 105, 80, 225, p.x, p.y)) {   // End Game selected
        tone(tonePin, hapticFeedbackToneFrequency, hapticFeedbackToneDuration);
        currentPage = '4';
        gameInPlay = false;
        drawGameOverPage();
      }
      goto base;
    }

    // Game Over Page
    if (currentPage == '4') {
      if (is_pressed(35, 235, 105, 335, p.x, p.y)) {   // Replay selected
        tone(tonePin, hapticFeedbackToneFrequency, hapticFeedbackToneDuration);
        currentPage = '2';
        drawStartPage(currentCategory);
      }
      if (is_pressed(35, 115, 105, 220, p.x, p.y)) {   // Categories selected
        tone(tonePin, hapticFeedbackToneFrequency, hapticFeedbackToneDuration);
        currentPage = '1';
        drawCategoriesPage();
        phraseFile.close();
      }
     if (is_pressed(35, -5, 105, 100, p.x, p.y)) {   // Home selected
        tone(tonePin, hapticFeedbackToneFrequency, hapticFeedbackToneDuration);
        currentPage = '0';
        drawHomePage();
        phraseFile.close();
      }
      goto base;
    }
  }

 gametimer:
  if (gameInPlay){
    while (gameTimeRemaining > 0) {
      unsigned long currentMillis = millis();
      unsigned long interval2;
      if (gameTimeRemaining <= gameTime*0.10) {
        interval2 = 125;
        toneDuration = 50;
      } else if (gameTimeRemaining <= gameTime*0.33) {
        interval2 = 250;
        toneDuration = 100;
      } else if (gameTimeRemaining <= gameTime*0.66) {
        interval2 = 500;
        toneDuration = 200;
      } else if (gameTimeRemaining <= maxGameTime) {
        interval2 = 1000;
        toneDuration = 300;
      }

      if (currentMillis - previousMillis1 >= interval1) {   //Every second that has passed, decrement remaning time by 1 second
        previousMillis1 += 1000;
        gameTimeRemaining--;
        Serial.print("Game time remaining: ");
        Serial.print(gameTimeRemaining);
        Serial.println(" seconds");
      }

      if (outputTone) { //Currently outputting a tone. Check if it's been long enough and turn off, if so...
        if (currentMillis - previousMillis2 >= toneDuration){
          previousMillis2 = currentMillis;
          noTone(tonePin);
          digitalWrite(motorPin,LOW);
          outputTone = false;
        }
      }else{            //Currently in a pause. Check if it's been long enough and turn on, if so...
        if (currentMillis - previousMillis2 >= interval2) {
          previousMillis2 = currentMillis;
          tone(tonePin, toneFrequency);
          digitalWrite(motorPin,HIGH);
          outputTone = true;
        }
      }
      goto base;
    }
    if (gameTimeRemaining <= 0) {
      delay(450);
      toneFrequency = 900;
      toneDuration = 2000;
      tone(tonePin, toneFrequency, toneDuration);
      digitalWrite(motorPin,HIGH);
      gameInPlay = false;
      currentPage = '4';
      drawGameOverPage();
      delay(toneDuration);
      digitalWrite(motorPin,LOW);
      }
  }
}