/************************************************************
■Macでメモリーカードをフォーマット。ファイルタイプをFAT32形式にする方法。
	https://blog.websandbag.com/entry/2018/02/15/040004
	
	sj note
		MS-DOS(FAT)
		★★★ くれぐれも、HDDを消してしまわないよう、細心の注意を払うこと ★★★
		
		元々、FAT16であったが、改めてmac上でディスクユーティリティを使い、FATでformatする(結果は、FAT16なのだが)必要があった。
		-> そうしないと、なぜかfileを書き込めなかったので。
		
■Seeed-Studio/ Music_Shield
	query = arduino MusicPlayer seeed api
		https://github.com/Seeed-Studio/Music_Shield


■Arduino freezes during setVolume call #5
	https://github.com/Seeed-Studio/Music_Shield/issues/5

■Simple MP3 Player
	https://steve.fi/hardware/simple-mp3-player/
	contents
		Because of that you will also be restricted to DOS-style 8.3 filenames.
	
	sj note
		実験の結果
			m00.mp3, m01.mp3, ...
		は読み込めなかった。
************************************************************/
#include <SD.h>
#include <SPI.h>

/************************************************************
compile switch
************************************************************/
// #define SINGLE_DEBUG
// #define SINGLE_DEBUG_AS_RECEIVER

/********************
check if you have set the compile switch correctly.
********************/
#ifdef SINGLE_DEBUG
	#ifdef SINGLE_DEBUG_AS_RECEIVER
		#error SingleDebug & SingleDebugAsReceiver not allowed to set at the same time.
	#endif
#endif

/************************************************************
Music
************************************************************/
#ifndef SINGLE_DEBUG_AS_RECEIVER
	#include <MusicPlayer.h>
#endif

/************************************************************
************************************************************/
// #define AUTO_SCAN

const int SD_SS = 10;

#ifdef SINGLE_DEBUG
	const int MAX_NUM_MUSICS = 1;
#else
	const int MAX_NUM_MUSICS = 5;
#endif

int NUM_MUSICS = 0;

int order[MAX_NUM_MUSICS];
int id_of_Music = 0;
int id_of_announce_WaitSensorRestart;
int id_of_announce_SuspectOfsError;
int id_of_announce_WillReset;
int id_of_announce_Recovered;


/********************
********************/
const int LED_PIN = 2; // 外付Led : showing start process complete.

const long vol_announce = 10;
// const long vol_min = 254;
const long vol_min = 200;
const long vol_max = 0;
float vol = (float)vol_min;

#ifdef SINGLE_DEBUG_AS_RECEIVER // あまり早いと、I2C master側で、動きが見えないので.
	const float T_fall_toEar = 3000;
	const float T_rise_toEar = 3000;
#else
	const float T_fall_toEar = 2000;
	const float T_rise_toEar = 80;
#endif

/********************
********************/
enum class SENSOR_STATE{
	ON_THE_TABLE,
	OFF_THE_TABLE,
	SUSPECT_OFS_ERROR,
	WILL_RESET,
	
	SETUP,
	WAIT_1ST_MESSAGE,
	WAIT_SENSOR_RESTART,
	
	NUM_STATES,
};

SENSOR_STATE SensorState = SENSOR_STATE::SETUP;
// String str_StateName[(int)SENSOR_STATE::NUM_STATES] = {"OnTheTable", "OffTheTable", "TareError",  "Wait_1stMessage", "Wait_SensorRestart"};

/********************
********************/
enum class MUSIC_STATE{
	STOP,
	VOL_UP,
	MAX_PLAY,
	VOL_DOWN,
	PAUSE,
	
	WAIT_SENSOR_RESTART,
	ANNOUNCE__WAIT_SENSOR_RESTART,
	SUSPECT_OFS_ERROR,
	ANNOUNCE__SUSPECT_OFS_ERROR,
	WILL_RESET,
	ANNOUNCE__WILL_RESET,
	
	DETECTED_SENSOR_RECOVERY,
	ANNOUNCE__RECOVERY,
	
	NUM_STATES,
};

MUSIC_STATE MusicState = MUSIC_STATE::STOP;
long t_MusicStatefrom = 0;

long t_LastUpdate = 0;

/********************
I2C
********************/
#include <Wire.h>

#define SLAVE_ADDRESS 0x08

boolean b_got_i2c = false;
uint8_t i2c_receivedState = 0;
long t_Last_I2C_Received_ms = 0;


/************************************************************
func
************************************************************/

/******************************
******************************/
void setup() {
	/********************
	********************/
	randomSeed(analogRead(0)); // player.begin();の前であれば、未接続
	
	/********************
	********************/
	pinMode(LED_PIN, OUTPUT);
	digitalWrite(LED_PIN, LOW);
	
	/********************
	********************/
	Serial.begin(9600);
	
	Wire.begin(SLAVE_ADDRESS);
	Wire.onReceive(onReceiveI2C);
	Wire.onRequest(onRequestI2C);
	
	/********************
	********************/
#ifdef SINGLE_DEBUG_AS_RECEIVER
	setup_SingleDebug_AsReceiver();
#else
	setup_Normal();
#endif
	
	/********************
	********************/
	digitalWrite(LED_PIN, HIGH);
	SensorState = SENSOR_STATE::WAIT_1ST_MESSAGE;
	// Serial.println("setup OK");
}

/******************************
******************************/
/*
void printError(char* chFileName, int line, char* chFuncName){
	Serial.println("> Error");
	Serial.println(chFileName);
	
	Serial.print("line:");
	Serial.println(line);
	
	Serial.print("func:");
	Serial.println(chFuncName);
}
*/

#ifdef SINGLE_DEBUG_AS_RECEIVER
	/******************************
	******************************/
	void setup_SingleDebug_AsReceiver(){
		/********************
		********************/
		NUM_MUSICS = 3; // 適当に決め打ち
		Serial.print("NUM=");
		Serial.println(NUM_MUSICS);
		
		if(NUM_MUSICS <= 0) { printError(__LINE__); while(1); }
		
		init_order();
		shuffle(order, NUM_MUSICS); // uncomment this to make the order random(fixed when boot).
		
		/********************
		********************/
		vol = (float)vol_min;
		
		/********************
		********************/
		id_of_Music							= 0;
		id_of_announce_WaitSensorRestart	= NUM_MUSICS;
		id_of_announce_SuspectOfsError		= id_of_announce_WaitSensorRestart + 1;
		id_of_announce_WillReset			= id_of_announce_SuspectOfsError + 1;
		id_of_announce_Recovered			= id_of_announce_WillReset + 1;
	}

#else	// #ifdef SINGLE_DEBUG_AS_RECEIVER
	/******************************
	******************************/
	void setup_Normal(){
		/********************
		********************/
		pinMode(SS, OUTPUT);
		if( !SD.begin(SD_SS) ) { printError(__LINE__); while(1); } // need this to use SD class like "SD.exists()".
		
		/********************
		********************/
		NUM_MUSICS = Count_NumOfMusics_inSD();
		Serial.print("NUM=");
		Serial.println(NUM_MUSICS);
		
		if(NUM_MUSICS <= 0) { printError(__LINE__); while(1); }
		
		init_order();
		shuffle(order, NUM_MUSICS); // uncomment this to make the order random(fixed when boot).
		
		/********************
		********************/
		player.begin();	//will initialize the hardware and set default mode to be normal.
		
		player.setPlayMode(PM_REPEAT_ONE);
		vol = (float)vol_min;
		player.setVolume((unsigned char)vol);
		
	#ifdef 	AUTO_SCAN
		// file nameの順番が狂ってしまった
		player.scanAndPlayAll();
		
	#else
		// 決め打ち
		enum { BUF_SIZE = 100, };
		for(int i = 0; i < NUM_MUSICS; i++){
			char buf[BUF_SIZE];
			sprintf(buf, "music_%02d.mp3", order[i]);
			
			Serial.print("> ");
			Serial.println(buf);
			
			if(SD.exists(buf) == true){
				player.addToPlaylist(buf);
			}else{
				Serial.print(buf);
				Serial.println(" not exist.");
			}
		}
	#endif
		
		/********************
		********************/
		if( !player.addToPlaylist("WaitRest.mp3") )	{ printError(__LINE__); while(1); }
		if( !player.addToPlaylist("OfsError.mp3") )	{ printError(__LINE__); while(1); }
		if( !player.addToPlaylist("WilReset.mp3") )	{ printError(__LINE__); while(1); }
		if( !player.addToPlaylist("Recovery.mp3") )	{ printError(__LINE__); while(1); }
		
		id_of_Music							= 0;
		id_of_announce_WaitSensorRestart	= NUM_MUSICS;
		id_of_announce_SuspectOfsError		= id_of_announce_WaitSensorRestart + 1;
		id_of_announce_WillReset			= id_of_announce_SuspectOfsError + 1;
		id_of_announce_Recovered			= id_of_announce_WillReset + 1;
		
		/********************
		********************/
		player.opStop();
		player.opSelectSong(order[id_of_Music]);
	}
#endif // #ifdef SINGLE_DEBUG_AS_RECEIVER

/******************************
******************************/
/*
void printError(char* chFileName, int line){
	Serial.println("> Error");
	Serial.println(chFileName);
	
	Serial.print("line:");
	Serial.println(line);
}
*/

/******************************
******************************/
void printError(int line){
	Serial.println("> Error");
	Serial.print("line:");
	Serial.println(line);
}

/******************************
******************************/
void onRequestI2C() {
	Wire.write((uint8_t)SensorState);
	Wire.write((uint8_t)MusicState);
}

/******************************
******************************/
void onReceiveI2C() {
	while(0 < Wire.available()){
		i2c_receivedState = Wire.read();
		b_got_i2c = true;
	}
}

/******************************
******************************/
void loop() {
	/********************
	Music Shieldの制御関数を呼び出す
	player.update()の方が良かったのでは？？
	********************/
#ifndef SINGLE_DEBUG_AS_RECEIVER
	player.play();
#endif
	
	/********************
	********************/
	StateChart_Sensor();
	StateChart_Music();
	
	/********************
	********************/
	if (0 < Serial.available())	keyPressed(Serial.read());
	
	/********************
	********************/
	t_LastUpdate = my_millis();
	
	delay(10);
}


/******************************
******************************/
long my_millis() {
	return (long)millis();
}

/******************************
******************************/
int Count_NumOfMusics_inSD(){
#ifdef SINGLE_DEBUG_AS_RECEIVER
	return 3; // 適当.
#else
	int counter = 0;
	while(counter < MAX_NUM_MUSICS){
		char buf[100];
		sprintf(buf, "music_%02d.mp3", counter);
		
		if(SD.exists(buf) == true)	counter++;
		else						break;
	}
	
	return counter;
#endif
}

/******************************
******************************/
void init_order()
{
	for(int i = 0; i < MAX_NUM_MUSICS; i++){
		order[i] = i;
	}
}

/******************************
description
	fisher yates法
		偏りをなくすため、回を追うごとに乱数範囲を狭めていくのがコツ
		http://www.fumiononaka.com/TechNotes/Flash/FN0212003.html
	
	random(min, max)
		http://www.musashinodenpa.com/arduino/ref/index.php?f=0&pos=2901
		
	randomSeed(seed)
		http://www.musashinodenpa.com/arduino/ref/index.php?f=0&pos=2867
******************************/
void shuffle(int* data, int size)
{
	for(int i = size - 1; 0 < i; i--){
		/********************
		int j = rand() % (i + 1);
		int j = (int)( ((double)rand() / ((double)RAND_MAX + 1)) * (i + 1) );
		********************/
		int j = random(0, size); // [min, max)
		
		/********************
		********************/
		int temp = data[i];
		data[i] = data[j];
		data[j] = temp;
	}
}

/******************************
******************************/
void print_volume(){
#ifdef SINGLE_DEBUG_AS_RECEIVER
	Serial.print("vol=");
	Serial.println(vol);
#else
	Serial.print(my_millis());
	Serial.print(",");
	Serial.print("vol=");
	Serial.println(vol);
/*
	// lib側にて : 結局、Registerに触りに行くのは、timer割り込み内でないとNG
	Serial.print("Vol=");
	Serial.print(player.getVolume_L());
	Serial.print(", ");
	Serial.println(player.getVolume_R());
*/
#endif
}

/******************************
******************************/
void keyPressed(char key){
#ifndef SINGLE_DEBUG_AS_RECEIVER
	switch(key){
		case 'p':
			player.opPlay();
			break;
			
		case 'a':
			player.opPause();
			break;
			
		case 'r':
			player.opResume();
			break;
			
		case 's':
			player.opStop();
			break;
			
		case 'g':
			print_volume();
			break;
			
		case 'u':
			player.opVolumeUp(); // 逆
			print_volume();
			break;
			
		case 'd':
			player.opVolumeDown(); // 逆
			print_volume();
			break;
			
		case 't':
			// player.opFirstSong();
			Serial.println(my_millis() - t_MusicStatefrom);
			break;
			
		case '0':
		case '1':
		case '2':
			Serial.print("S>");
			Serial.println(key);
			
			i2c_receivedState = key - '0';
			b_got_i2c = true;
			
			// player.opSelectSong(key - '0');
			break;
			
		case '3':
			player.setVolume(0);
			break;
			
		case '4':
			player.setVolume(50);
			break;
	}
#endif
}

/******************************
******************************/
void StateChart_Sensor() {
#ifdef SINGLE_DEBUG
	long timeout = 60000 * 5; // 5 min
#else
	long timeout = 1000;
	if(SensorState == SENSOR_STATE::WAIT_1ST_MESSAGE) timeout = 5000;
#endif
	
	switch(SensorState){
		case SENSOR_STATE::ON_THE_TABLE:
		case SENSOR_STATE::OFF_THE_TABLE:
		case SENSOR_STATE::SUSPECT_OFS_ERROR:
		case SENSOR_STATE::WAIT_1ST_MESSAGE:
			if(b_got_i2c){
				SensorState_Transition_to_Received();
				b_got_i2c = false;
			}else if(timeout < my_millis() - t_Last_I2C_Received_ms){
				SensorState = SENSOR_STATE::WAIT_SENSOR_RESTART;
			}
			break;
			
		case SENSOR_STATE::WILL_RESET:
		case SENSOR_STATE::WAIT_SENSOR_RESTART:
			if(b_got_i2c){
				SensorState_Transition_to_Received();
				b_got_i2c = false;
			}
			break;
	}
}

/******************************
******************************/
void SensorState_Transition_to_Received(){
	t_Last_I2C_Received_ms = my_millis();
	
	if( i2c_receivedState <= (int)SENSOR_STATE::WILL_RESET ){
		SensorState = (SENSOR_STATE)i2c_receivedState;
#if defined(SINGLE_DEBUG) || defined(SINGLE_DEBUG_AS_RECEIVER)
		Serial.print("Sensor:");
		Serial.println((int)SensorState);
#endif
	}
}

/******************************
******************************/
void StateChart_Music() {
	switch(MusicState){
		case MUSIC_STATE::STOP:
			Process_MusicState__STOP();
			break;
			
		case MUSIC_STATE::VOL_UP:
			Process_MusicState__VOL_UP();
			break;
			
		case MUSIC_STATE::MAX_PLAY:
			Process_MusicState__MAX_PLAY();
			break;
			
		case MUSIC_STATE::VOL_DOWN:
			Process_MusicState__VOL_DOWN();
			break;
			
		case MUSIC_STATE::PAUSE:
			Process_MusicState__PAUSE();
			break;
			
		case MUSIC_STATE::WAIT_SENSOR_RESTART:
			Process_MusicState__WAIT_SENSOR_RESTART();
			break;
			
		case MUSIC_STATE::ANNOUNCE__WAIT_SENSOR_RESTART:
			Process_MusicState__ANNOUNCE__WAIT_SENSOR_RESTART();
			break;
			
		case MUSIC_STATE::SUSPECT_OFS_ERROR:
			Process_MusicState__SUSPECT_OFS_ERROR();
			break;
			
		case MUSIC_STATE::ANNOUNCE__SUSPECT_OFS_ERROR:
			Process_MusicState__ANNOUNCE__SUSPECT_OFS_ERROR();
			break;
			
		case MUSIC_STATE::WILL_RESET:
			Process_MusicState__WILL_RESET();
			break;
			
		case MUSIC_STATE::ANNOUNCE__WILL_RESET:
			Process_MusicState__ANNOUNCE__WILL_RESET();
			break;
			
		case MUSIC_STATE::DETECTED_SENSOR_RECOVERY:
			Process_MusicState__DETECTED_SENSOR_RECOVERY();
			break;
			
		case MUSIC_STATE::ANNOUNCE__RECOVERY:
			Process_MusicState__ANNOUNCE__RECOVERY();
			break;
	}
}

/******************************
******************************/
void MusicState_Print_and_DoTransition(MUSIC_STATE from, MUSIC_STATE to){
	MusicState_PrintTransition(from, to);
	
	MusicState = to;
	t_MusicStatefrom = my_millis();
}

/******************************
******************************/
void MusicState_PrintTransition(MUSIC_STATE from, MUSIC_STATE to){
	Serial.print("fr:");
	Serial.print((int)from);
	Serial.print("->to:");
	Serial.print((int)to);
	Serial.print(" (");
	Serial.print(my_millis() - t_MusicStatefrom);
	Serial.println(")");
}

/******************************
******************************/
void MusicState_Transition_to_PreAnnounce(MUSIC_STATE PreState_To_Announce){
	/********************
	********************/
	int id_of_announce = 0;
	
	switch(PreState_To_Announce){
		case MUSIC_STATE::WAIT_SENSOR_RESTART:
			id_of_announce = id_of_announce_WaitSensorRestart;
			break;
		case MUSIC_STATE::SUSPECT_OFS_ERROR:
			id_of_announce = id_of_announce_SuspectOfsError;
			break;
		case MUSIC_STATE::WILL_RESET:
			id_of_announce = id_of_announce_WillReset;
			break;
		case MUSIC_STATE::DETECTED_SENSOR_RECOVERY:
			id_of_announce = id_of_announce_Recovered;
			break;
		default:
			id_of_announce = id_of_announce_Recovered;
			break;
	}
	
	/********************
	********************/
#ifndef SINGLE_DEBUG_AS_RECEIVER
	player.opStop();
	player.opSelectSong(id_of_announce);
	player.setPlayMode(PM_JUST_ONE);
	player.setVolume(vol_announce);
#endif
	
	/********************
	********************/
	MusicState_Print_and_DoTransition(MusicState, PreState_To_Announce);
}

/******************************
******************************/
boolean MusicState_TransitionToPreAnnounce_IfAnyError(boolean b_WaitSensorRestart, boolean b_SuspectOfsError, boolean WillReset){
	if( b_WaitSensorRestart	&& (SensorState == SENSOR_STATE::WAIT_SENSOR_RESTART) )		{	MusicState_Transition_to_PreAnnounce(MUSIC_STATE::WAIT_SENSOR_RESTART);	return true;	}
	if( b_SuspectOfsError	&& (SensorState == SENSOR_STATE::SUSPECT_OFS_ERROR) )		{	MusicState_Transition_to_PreAnnounce(MUSIC_STATE::SUSPECT_OFS_ERROR);	return true;	}
	if( WillReset			&& (SensorState == SENSOR_STATE::WILL_RESET) )				{	MusicState_Transition_to_PreAnnounce(MUSIC_STATE::WILL_RESET);			return true;	}
	
	return false;
}

/******************************
******************************/
void Process_MusicState__STOP(){
	if( MusicState_TransitionToPreAnnounce_IfAnyError(true, true, true) ){
		// process completed in the function above.
	}else if(SensorState == SENSOR_STATE::OFF_THE_TABLE){
		/********************
		********************/
#ifndef SINGLE_DEBUG_AS_RECEIVER
		player.opPlay();
#endif
		
		/********************
		********************/
		MusicState_Print_and_DoTransition(MusicState, MUSIC_STATE::VOL_UP);
	}else{
		/********************
		********************/
		// nothing
	}
}

/******************************
******************************/
void Process_MusicState__VOL_UP(){
	if( MusicState_TransitionToPreAnnounce_IfAnyError(true, true, true) ){
		// process completed in the function above.
	}else if(SensorState == SENSOR_STATE::ON_THE_TABLE){
		/********************
		********************/
		// nothing
		
		/********************
		********************/
		MusicState_Print_and_DoTransition(MusicState,  MUSIC_STATE::VOL_DOWN);
		
	}else{
		/********************
		********************/
		unsigned char LastSetVol = (unsigned char)vol;
		
		float dvol = (float)vol_min / T_rise_toEar * (float)(my_millis() - t_LastUpdate);
		vol -= dvol;
		
		if(vol <= vol_max)	vol = vol_max;
#ifndef SINGLE_DEBUG_AS_RECEIVER
		if((unsigned char)vol != LastSetVol)	player.setVolume((unsigned char)vol);
#endif
		// print_volume();
		
		if(vol <= vol_max)	MusicState_Print_and_DoTransition(MusicState,  MUSIC_STATE::MAX_PLAY);
	}
}

/******************************
******************************/
void Process_MusicState__MAX_PLAY(){
	if( MusicState_TransitionToPreAnnounce_IfAnyError(true, true, true) ){
		// process completed in the function above.
	}else if(SensorState == SENSOR_STATE::ON_THE_TABLE){
		/********************
		********************/
		// nothing
		
		/********************
		********************/
		MusicState_Print_and_DoTransition(MusicState,  MUSIC_STATE::VOL_DOWN);
		
	}else{
		/********************
		********************/
		// nothing
	}
}

/******************************
******************************/
void Process_MusicState__VOL_DOWN(){
	if( MusicState_TransitionToPreAnnounce_IfAnyError(true, true, true) ){
		// process completed in the function above.
	}else if(SensorState == SENSOR_STATE::OFF_THE_TABLE){
		/********************
		********************/
		// nothing
		
		/********************
		********************/
		MusicState_Print_and_DoTransition(MusicState,  MUSIC_STATE::VOL_UP);
		
	}else{
		/********************
		********************/
		unsigned char LastSetVol = (unsigned char)vol;
		
		float dvol = (float)vol_min / T_fall_toEar * (float)(my_millis() - t_LastUpdate);
		vol += dvol;
		
		if(vol_min <= vol)	vol = vol_min;
#ifndef SINGLE_DEBUG_AS_RECEIVER
		if((unsigned char)vol != LastSetVol)	player.setVolume((unsigned char)vol);
#endif
		// print_volume();
		
		if(vol_min <= vol){
#ifndef SINGLE_DEBUG_AS_RECEIVER
			player.opPause();
#endif
			
			MusicState_Print_and_DoTransition(MusicState,  MUSIC_STATE::PAUSE);
		}
	}
}

/******************************
******************************/
void Process_MusicState__PAUSE(){
	if( MusicState_TransitionToPreAnnounce_IfAnyError(true, true, true) ){
		// process completed in the function above.
	}else if(SensorState == SENSOR_STATE::OFF_THE_TABLE){
		/********************
		********************/
#ifndef SINGLE_DEBUG_AS_RECEIVER
		player.opResume();
#endif
		
		/********************
		********************/
		MusicState_Print_and_DoTransition(MusicState,  MUSIC_STATE::VOL_UP);
		
	// }else if(500 < my_millis() - t_MusicStatefrom){
	}else if(60000 < my_millis() - t_MusicStatefrom){
		/********************
		********************/
#ifndef SINGLE_DEBUG_AS_RECEIVER
		player.opStop();
#endif
		id_of_Music++;
		if(NUM_MUSICS <= id_of_Music) id_of_Music = 0;
#ifndef SINGLE_DEBUG_AS_RECEIVER
		player.opSelectSong(order[id_of_Music]);
#endif
		
		/********************
		********************/
		MusicState_Print_and_DoTransition(MusicState,  MUSIC_STATE::STOP);
	}else{
		/********************
		********************/
		// nothing
	}
}

/******************************
******************************/
void Process_MusicState__WAIT_SENSOR_RESTART(){
	if( MusicState_TransitionToPreAnnounce_IfAnyError(false, true, true) ){
		// process completed in the function above.
	}else if( (SensorState == SENSOR_STATE::ON_THE_TABLE) || (SensorState == SENSOR_STATE::OFF_THE_TABLE) ){
		MusicState_Transition_to_PreAnnounce(MUSIC_STATE::DETECTED_SENSOR_RECOVERY);
	}else if(1000 < my_millis() - t_MusicStatefrom){
		
#ifndef SINGLE_DEBUG_AS_RECEIVER
		player.opPlay();
#endif
		
		MusicState_Print_and_DoTransition(MusicState,  MUSIC_STATE::ANNOUNCE__WAIT_SENSOR_RESTART);
		
	}else{
		/********************
		********************/
		// nothing
	}
}

/******************************
******************************/
void Process_MusicState__ANNOUNCE__WAIT_SENSOR_RESTART(){
	if( MusicState_TransitionToPreAnnounce_IfAnyError(false, true, true) ){
		// process completed in the function above.
	}else if( (SensorState == SENSOR_STATE::ON_THE_TABLE) || (SensorState == SENSOR_STATE::OFF_THE_TABLE) ){
		MusicState_Transition_to_PreAnnounce(MUSIC_STATE::DETECTED_SENSOR_RECOVERY);
	}else{
		/********************
		********************/
		// nothing
	}
}

/******************************
******************************/
void Process_MusicState__SUSPECT_OFS_ERROR(){
	if( MusicState_TransitionToPreAnnounce_IfAnyError(true, false, true) ){
		// process completed in the function above.
	}else if( (SensorState == SENSOR_STATE::ON_THE_TABLE) || (SensorState == SENSOR_STATE::OFF_THE_TABLE) ){
		MusicState_Transition_to_PreAnnounce(MUSIC_STATE::DETECTED_SENSOR_RECOVERY);
	}else if(1000 < my_millis() - t_MusicStatefrom){
		
#ifndef SINGLE_DEBUG_AS_RECEIVER
		player.opPlay();
#endif
		
		MusicState_Print_and_DoTransition(MusicState,  MUSIC_STATE::ANNOUNCE__SUSPECT_OFS_ERROR);
		
	}else{
		/********************
		********************/
		// nothing
	}
}

/******************************
******************************/
void Process_MusicState__ANNOUNCE__SUSPECT_OFS_ERROR(){
	if( MusicState_TransitionToPreAnnounce_IfAnyError(true, false, true) ){
		// process completed in the function above.
	}else if( (SensorState == SENSOR_STATE::ON_THE_TABLE) || (SensorState == SENSOR_STATE::OFF_THE_TABLE) ){
		MusicState_Transition_to_PreAnnounce(MUSIC_STATE::DETECTED_SENSOR_RECOVERY);
	}else{
		/********************
		********************/
		// nothing
	}
}

/******************************
******************************/
void Process_MusicState__WILL_RESET(){
	if( MusicState_TransitionToPreAnnounce_IfAnyError(true, true, false) ){
		// process completed in the function above.
	}else if( (SensorState == SENSOR_STATE::ON_THE_TABLE) || (SensorState == SENSOR_STATE::OFF_THE_TABLE) ){
		MusicState_Transition_to_PreAnnounce(MUSIC_STATE::DETECTED_SENSOR_RECOVERY);
	}else if(1000 < my_millis() - t_MusicStatefrom){
		
#ifndef SINGLE_DEBUG_AS_RECEIVER
		player.opPlay();
#endif
		
		MusicState_Print_and_DoTransition(MusicState,  MUSIC_STATE::ANNOUNCE__WILL_RESET);
		
	}else{
		/********************
		********************/
		// nothing
	}
}

/******************************
******************************/
void Process_MusicState__ANNOUNCE__WILL_RESET(){
	if( MusicState_TransitionToPreAnnounce_IfAnyError(true, true, false) ){
		// process completed in the function above.
	}else if( (SensorState == SENSOR_STATE::ON_THE_TABLE) || (SensorState == SENSOR_STATE::OFF_THE_TABLE) ){
		MusicState_Transition_to_PreAnnounce(MUSIC_STATE::DETECTED_SENSOR_RECOVERY);
	}else{
		/********************
		********************/
		// nothing
	}
}

/******************************
******************************/
void Process_MusicState__DETECTED_SENSOR_RECOVERY(){
	if( MusicState_TransitionToPreAnnounce_IfAnyError(true, true, true) ){
		// process completed in the function above.
	}else if(1000 < my_millis() - t_MusicStatefrom){
		
#ifndef SINGLE_DEBUG_AS_RECEIVER
		player.opPlay();
#endif
		
		MusicState_Print_and_DoTransition(MusicState,  MUSIC_STATE::ANNOUNCE__RECOVERY);
		
	}else{
		/********************
		********************/
		// nothing
	}
}

/******************************
******************************/
void Process_MusicState__ANNOUNCE__RECOVERY(){
	if( MusicState_TransitionToPreAnnounce_IfAnyError(true, true, true) ){
		// process completed in the function above.
		
#ifndef SINGLE_DEBUG_AS_RECEIVER
	}else if( !player.isPlaying() ){
#else
	}else if(1000 < my_millis() - t_MusicStatefrom){
#endif
		id_of_Music = 0;
		vol = vol_min;
#ifndef SINGLE_DEBUG_AS_RECEIVER
		player.opStop();
		player.setPlayMode(PM_REPEAT_ONE);
		player.opSelectSong(order[id_of_Music]);
		player.setVolume((unsigned char)vol);
#endif
		
		MusicState_Print_and_DoTransition(MusicState, MUSIC_STATE::STOP);
	}else{
		/********************
		********************/
		// nothing
	}
}



