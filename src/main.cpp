// #include <Arduino.h>
// #include <async/WavPlayer.h>
// #include <async/Executor.h>
// #include <async/ByteStream.h>
// #include "audio_file.cpp"
// #include "M62429.h"


// using namespace async;

// Executor executor;
// WavPlayer player(19, 22, 25);
// M62429 chip (26, 27); 

// void setup() {
//   Serial.begin(115200);
//   Serial.println("setup");
//   chip.setVolumeCh1(13);

//   executor.start();
//   // add interrupts
//   executor.add(&player);

//   player.setVolume(0, 0.01);

//   player.play(0, new ByteStream(audioHex, sizeof(audioHex)));

//   player.onEvent([](int trackNum, WavPlayerEvent event) {
//     Serial.println(event);
//     if(event == TRACK_STOPPED) {
//         player.play(0, new ByteStream(audioHex, sizeof(audioHex)));
//     }
//   });
// }

// void loop() {
//     executor.tick();
// }