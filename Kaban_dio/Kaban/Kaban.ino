// {"numberServo":0, "angle":70, "speed":5, "current":1, "pkp": 16, "pki": 0.0, "pkd": 0.0, "vkp": 0.007, "ikp": 30, "iki": 100} 
#include "ArduinoJson.h"

#define MIN_POS         12     // границы движения сервопривода в градусах
#define MAX_POS         139
#define MIN_MAX_SPEED   30*360/60 // ограничение скорости 30 об/мин, тут же переводим в 180 град/сек   //
#define MIN_MAX_CURRENT 0.4     // в амперах

#define ENCODER_PIN     7    // пин энкодера AS5600
#define CURRENT_SENS_PIN  6  // пин датчика тока (AC712)
#define MX1508_IN1_PIN  10    // пины драйвера двигателей
#define MX1508_IN2_PIN  9  
#define pinA 2
#define pinB 3 

#define ENCODER_SCALE   270.f/1024.f  // макрос перевода угла потенциометра или энкодера из АЦП попугаев [0:1024] в градусы [0:270] 
#define CURRENT_SCALE   0.185  // шкала датчика тока: 0.185В на выходе на 1А 
#define SPEED_SCALE 1

float palpha = 1.0;
float ialpha = 0.2;

float pkP = 3.0;   // пропорциональный коэффициент PID-регулятора по положинию
float pkI = 0.0;   // интегральный коэффициент PID-регулятора по положинию    
float pkD = 0.0;   // дифференциальный коэффициент PID-регулятора по положинию 

float vkP = 10.0;   // пропорциональный коэффициент PID-регулятора по скорости
float vkI = 0.0;   // интегральный коэффициент PID-регулятора по скорости    
float vkD = 0.0;   // дифференциальный коэффициент PID-регулятора по скорости

float ikP = 10.0;   // пропорциональный коэффициент PID-регулятора по току
float ikI = 1.0;   // интегральный коэффициент PID-регулятора по току    
float ikD = 0.0;   // дифференциальный коэффициент PID-регулятора по току

	// json буфер для пакетов от ПК
StaticJsonDocument<200> jsondoc;

//переменные 
float numberServo = 0;  // задаваемый номер сервопривода
float position = 0;     // задаваемое положение (градусы)
float speed = 0;        // задаваемая PIDом положения скорость сервопривода (градусы/с)
float current = 0;      // задаваемый PIDом скорости ток сервопривода (амперы)
int16_t motorPwm = 0;   // заполнение ШИМ, подаваемое на мотор
int16_t command = 0;
int16_t error = 0;

/*volatile long pause    = 50;  // Пауза для борьбы с дребезгом
volatile long lastTurn = 0;   // Переменная для хранения времени последнего изменения
volatile int pinAValue = 0;   // Переменные хранящие состояние пина, для экономии времени
volatile int pinBValue = 0;   // Переменные хранящие состояние пина, для экономии времени
volatile int count = 0;       // Счетчик оборотов
int actualcount    = 0;       // Временная переменная определяющая изменение основного счетчика
volatile int state = 0;       // Статус одного шага - от 0 до 4 в одну сторону, от 0 до -4 - в другую
int RPM;*/

float realPosition = 0; // реальное положение сервопривода (градусы)
float realSpeed = 0;    // реальная скорость сервопривода (градусы/с)
float realCurrent = 0;  // реальный ток сервопривода (амперы)

uint32_t pidTimer = 0;  // таймер обновления pid-регулятора
uint32_t commTimer = 0;  
uint32_t errorTimer = 0;

void setup(){
  Serial.begin(9600);
  pinMode(ENCODER_PIN, INPUT); 
  pinMode(CURRENT_SENS_PIN, INPUT);
  pinMode(MX1508_IN1_PIN, OUTPUT); 
  pinMode(MX1508_IN2_PIN, OUTPUT);
  //pinMode(pinA, INPUT);           // Пины в режим приема INPUT
  //pinMode(pinB, INPUT);           // Пины в режим приема INPUT

  //attachInterrupt(0, pinA, CHANGE);  // Настраиваем обработчик прерываний по изменению сигнала
  //attachInterrupt(1, pinB, CHANGE);  // Настраиваем обработчик прерываний по изменению сигнала 

  setMotorPwm(0);
  pidTimer = millis();
  commTimer = millis();
}

void loop(){
  if (Serial.available() > 0){
    DeserializationError err = deserializeJson(jsondoc, Serial);
    if (err == DeserializationError::Ok){
      numberServo = (float)jsondoc["numberServo"];
      position = (float)jsondoc["angle"];
      speed = (float)jsondoc["speed"];
      current = (float)jsondoc["current"];
      pkP = (float)jsondoc["pkp"];
      pkI = (float)jsondoc["pki"];
      pkD = (float)jsondoc["pkd"];
      vkP = (float)jsondoc["vkp"];
      ikP = (float)jsondoc["ikp"];
      ikI = (float)jsondoc["iki"];
      command = (int)jsondoc["command"];
      position = constrain(position, MIN_POS, MAX_POS); 
    }
  }
  else {while (Serial.available() > 0) Serial.read();}
  if (millis() - commTimer >= 10){
    if (command == 1)
    {
      position = MIN_POS;
    }
    else if (command == 2)
    {
      position = MAX_POS;
    }
  }
  if (millis() - pidTimer >= 10){   
    // пересчитываем все параметры
    float dt = (millis()-pidTimer)/1000.f;  // пройденное с прошлого раза время
    float newPosition = getAngle(); // получаем новое положение
    realSpeed = (newPosition - realPosition)/dt;  // получаем скорость с оптического энкодера(доделать)
    realPosition = newPosition;
    realCurrent = getCurrent();
    
    float temp_ki = pkI;
    if(abs(speed) == MIN_MAX_SPEED) temp_ki = 0.f; // если скорость ушла в насыщение, то отключаем интегральную составляющую
    speed = ppid(realPosition, position, pkP, temp_ki, pkD, dt);  // получаем скорость с PID-регулятора
    speed = constrain(speed, -MIN_MAX_SPEED, MIN_MAX_SPEED);  // ограничиваем скорость до допустимого диапазона

    temp_ki = vkI;
    if(abs(current) == MIN_MAX_CURRENT) temp_ki = 0.f; // если ток ушел в насыщение, то отключаем интегральную составляющую
    current = vpid(realSpeed, speed, vkP, temp_ki, vkD, dt);  // получаем ток с PID-регулятора
    current = constrain(current, -MIN_MAX_CURRENT, MIN_MAX_CURRENT);  // ограничиваем ток до допустимого диапазона

    temp_ki = ikI;
    if(abs(motorPwm) == 255) temp_ki = 0.f; // если заполнение ШИМ ушло в насыщение, то отключаем интегральную составляющую
    motorPwm = (int16_t)ipid(realCurrent, current, ikP, temp_ki, ikD, dt);  // получаем заполнение ШИМ с PID-регулятора
    motorPwm = constrain(motorPwm, -255, 255);  // ограничиваем заполнение до допустимого диапазона
    
    setMotorPwm(motorPwm);  // подаем ШИМ на мотор
    pidTimer = millis();
    if (realSpeed != speed)
    {
      error = 2;
    }
    Serial.print(realPosition, DEC); // выводим реальную позицию 
    Serial.print(',');  
    Serial.println(position, DEC);   // и заданное положение
  }
  if (realPosition != position) {
    error = 1;
  }
}

float pid(float input, float trgt, float kp, float ki, float kd, float dt, float* integral, float* lasterror){
  if(dt == 0.f){
    *integral = 0.f;
    *lasterror = 0.f; 
    return 0.f;
  }
  
  float error = trgt - input;   // получаем ощибку регулирования
  *integral += ki * error * dt;  // пересчитываем интегральную сумму
  float diff = (error - *lasterror) / dt;  // ищем дифференциал

  *lasterror = error;
  return kp * error + *integral + kd * diff;   // считаем и выводим результат
}


float ppid(float input, float trgt, float kp, float ki, float kd, float dt){
  static float pintegral = 0.f;  // храним значение суммы интегральной компоненты 
  static float plastError = 0.f; // и предыдущую ошибку регулирования для дифференциирования
  return pid(input, trgt, kp, ki, kd, dt, &pintegral, &plastError);  
}


float vpid(float input, float trgt, float kp, float ki, float kd, float dt){
  static float vintegral = 0.f;  // храним значение суммы интегральной компоненты 
  static float vlastError = 0.f; // и предыдущую ошибку регулирования для дифференциирования
  return pid(input, trgt, kp, ki, kd, dt, &vintegral, &vlastError);  
}


float ipid(float input, float trgt, float kp, float ki, float kd, float dt){
  static float iintegral = 0.f;  // храним значение суммы интегральной компоненты 
  static float ilastError = 0.f; // и предыдущую ошибку регулирования для дифференциирования
  return pid(input, trgt, kp, ki, kd, dt, &iintegral, &ilastError);  
}

float lpFilter(float value, float oldValue, float alp){
  return oldValue*(1.f-alp)+ alp*value;
}

void setMotorPwm(int16_t pwm)//Переделать ШИМ
{
  if(pwm >= 0){
    analogWrite(MX1508_IN1_PIN, LOW);  
    analogWrite(MX1508_IN2_PIN, abs(pwm));
  } 
  else {
    analogWrite(MX1508_IN1_PIN, abs(pwm));  
    analogWrite(MX1508_IN2_PIN, LOW);
  }
}

float getCurrent(){
  static float oldCurr = 0;
  float curr = (analogRead(CURRENT_SENS_PIN) * 5.f/1024.f - 2.5)/CURRENT_SCALE;
  curr = lpFilter(curr, oldCurr, ialpha);   // фильтруем показания с потенциометра, если надо
  oldCurr = curr;
  return 0.3;
}

float getAngle(){ // функция получения угла с потенциометра/энкодера
  static float oldAngle = 0;
  float angle = ENCODER_SCALE * analogRead(ENCODER_PIN);
  angle = lpFilter(angle, oldAngle, palpha);   // фильтруем показания с потенциометра, если надо
  oldAngle = angle;
  return angle;
}
//Оптический
/*void A()
{
  if (micros() - lastTurn < pause) return;  // Если с момента последнего изменения состояния не прошло
  // достаточно времени - выходим из прерывания
  pinAValue = digitalRead(pinA);            // Получаем состояние пинов A и B
  pinBValue = digitalRead(pinB);

  cli();    // Запрещаем обработку прерываний, чтобы не отвлекаться
  if (state == 0  && !pinAValue &&  pinBValue || state == 2  && pinAValue && !pinBValue) {
    state += 1; // Если выполняется условие, наращиваем переменную state
    lastTurn = micros();
  }
  if (state == -1 && !pinAValue && !pinBValue || state == -3 && pinAValue &&  pinBValue) {
    state -= 1; // Если выполняется условие, наращиваем в минус переменную state
    lastTurn = micros();
  }
  setCount(state); // Проверяем не было ли полного шага из 4 изменений сигналов (2 импульсов)
  sei(); // Разрешаем обработку прерываний

  if (pinAValue && pinBValue && state != 0) state = 0; // Если что-то пошло не так, возвращаем статус в исходное состояние
}
void B()
{
  if (micros() - lastTurn < pause) return;
  pinAValue = digitalRead(pinA);
  pinBValue = digitalRead(pinB);

  cli();
  if (state == 1 && !pinAValue && !pinBValue || state == 3 && pinAValue && pinBValue) {
    state += 1; // Если выполняется условие, наращиваем переменную state
    lastTurn = micros();
  }
  if (state == 0 && pinAValue && !pinBValue || state == -2 && !pinAValue && pinBValue) {
    state -= 1; // Если выполняется условие, наращиваем в минус переменную state
    lastTurn = micros();
  }
  setCount(state); // Проверяем не было ли полного шага из 4 изменений сигналов (2 импульсов)
  sei();
  
  if (pinAValue && pinBValue && state != 0) state = 0; // Если что-то пошло не так, возвращаем статус в исходное состояние
}

void setCount(int state) {          // Устанавливаем значение счетчика
  if (state == 3 || state == -3) {  // Если переменная state приняла заданное значение приращения
    count += (int)(state / 3);      // Увеличиваем/уменьшаем счетчик
    lastTurn = micros();
    RPM = 60/((float)(micros()-lastTurn)/1000000);            // Запоминаем последнее изменение
  }
}*/
