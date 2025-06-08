// #include <Arduino.h>
// #include <async/WavPlayer.h>
// #include <async/Executor.h>
// #include <async/ByteStream.h>
// #include "audio_file.cpp"


// using namespace async;

// Executor executor;
// WavPlayer player(19, 22, 25);

// void setup() {
//   Serial.begin(115200);
//   Serial.println("setup");

//   executor.start();
//   // add interrupts
//   executor.add(&player);

//   player.setVolume(0, 0.5);

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