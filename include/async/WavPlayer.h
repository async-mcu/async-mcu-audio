#include <Arduino.h>
#include <driver/i2s.h>
#include <async/Tick.h>
#include <async/Stream.h>
#include <async/Function.h>

namespace async {
    enum WavPlayerEvent {
        TRACK_STARTED,
        TRACK_STOPPED,
        TRACK_PAUSED,
        TRACK_RESUMED
    };

    typedef Function<void(int trackNum, WavPlayerEvent event)> WavPlayerCallback;

    class WavPlayer : public Tick {
    private:
        struct AudioTrack {
            Stream* stream;
            bool isPlaying;
            bool isPaused;
            float volume;
            float fadeVolume;
            int16_t* buffer;
            size_t bufferPos;
            bool loop;
        };

        static const int MAX_TRACKS = 4;
        AudioTrack tracks[MAX_TRACKS];
        
        const int bckPin;
        const int wsPin;
        const int dataOutPin;
        const uint32_t sampleRate;
        bool initialized;
        int16_t* mixBuffer;
        const size_t mixBufferSize = 512;
        WavPlayerCallback eventCallback;
        
    public:
        WavPlayer(int bck = 26, int ws = 25, int dataOut = 22, uint32_t sampleRate = 32000) 
            : bckPin(bck), wsPin(ws), dataOutPin(dataOut), 
            initialized(false), eventCallback(nullptr), sampleRate(sampleRate) {
            
            for (int i = 0; i < MAX_TRACKS; i++) {
                tracks[i] = {
                    .stream = nullptr,
                    .isPlaying = false,
                    .isPaused = false,
                    .volume = 1.0f,
                    .fadeVolume = 0.0f,
                    .buffer = nullptr,
                    .bufferPos = 0,
                    .loop = false
                };
            }
            
            mixBuffer = (int16_t*)malloc(mixBufferSize * sizeof(int16_t));
        }
        
        ~WavPlayer() {
            cancel();
            free(mixBuffer);
        }
        
        bool start() override {
            Serial.println("start");
            if (initialized) return true;
            
            i2s_config_t i2s_config = {
                .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
                .sample_rate = sampleRate,
                .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
                .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
                .communication_format = I2S_COMM_FORMAT_STAND_I2S,
                .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
                .dma_buf_count = 8,
                .dma_buf_len = 1024,
                .use_apll = false,
                .tx_desc_auto_clear = true,
            };
            
            i2s_pin_config_t pin_config = {
                .bck_io_num = bckPin,
                .ws_io_num = wsPin,
                .data_out_num = dataOutPin,
                .data_in_num = I2S_PIN_NO_CHANGE
            };
            
            if (i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL) != ESP_OK) {
                return false;
            }
            
            if (i2s_set_pin(I2S_NUM_0, &pin_config) != ESP_OK) {
                i2s_driver_uninstall(I2S_NUM_0);
                return false;
            }
            
            for (int i = 0; i < MAX_TRACKS; i++) {
                tracks[i].buffer = (int16_t*)malloc(mixBufferSize * sizeof(int16_t));
                if (!tracks[i].buffer) {
                    cancel();
                    return false;
                }
            }
            
            initialized = true;
            return true;
        }
        
        bool cancel() override {
            Serial.println("cancel");
            if (!initialized) return false;
            
            for (int i = 0; i < MAX_TRACKS; i++) {
                stop(i);
                free(tracks[i].buffer);
                tracks[i].buffer = nullptr;
            }
            
            i2s_driver_uninstall(I2S_NUM_0);
            initialized = false;
            return true;
        }
        
        bool play(int trackNum, Stream* stream) {
            if (trackNum < 0 || trackNum >= MAX_TRACKS || !initialized || !stream) return false;
            
            //stop(trackNum);
            stream->seek(44);
            
            tracks[trackNum] = {
                .stream = stream,
                .isPlaying = true,
                .isPaused = false,
                .volume = tracks[trackNum].volume,
                .fadeVolume = 0,
                .buffer = tracks[trackNum].buffer,
                .bufferPos = mixBufferSize, // Force buffer refill
                .loop = false
            };
            
            if (eventCallback) eventCallback(trackNum, TRACK_STARTED);
            return true;
        }

        bool loop(int trackNum, Stream* stream) {
            if (trackNum < 0 || trackNum >= MAX_TRACKS || !initialized || !stream) return false;
            
            //stop(trackNum);
            stream->seek(44);
            
            tracks[trackNum] = {
                .stream = stream,
                .isPlaying = true,
                .isPaused = false,
                .volume = tracks[trackNum].volume,
                .fadeVolume = 0,
                .buffer = tracks[trackNum].buffer,
                .bufferPos = mixBufferSize, // Force buffer refill
                .loop = true
            };
            
            if (eventCallback) eventCallback(trackNum, TRACK_STARTED);
            return true;
        }
        
        void onEvent(WavPlayerCallback callback) {
            eventCallback = callback;
        }
        
        void pause(int trackNum) {
            if (!isValidTrack(trackNum) || tracks[trackNum].isPaused) return;
            tracks[trackNum].isPaused = true;
            if (eventCallback) eventCallback(trackNum, TRACK_PAUSED);
        }
        
        void resume(int trackNum) {
            if (!isValidTrack(trackNum) || !tracks[trackNum].isPaused) return;
            tracks[trackNum].isPaused = false;
            if (eventCallback) eventCallback(trackNum, TRACK_RESUMED);
        }
        
        void stop(int trackNum) {
            if (!isValidTrack(trackNum)) return;
            
            tracks[trackNum].isPlaying = false;
            tracks[trackNum].isPaused = false;
            
            if (eventCallback) eventCallback(trackNum, TRACK_STOPPED);
        }
        
        bool isPlaying(int trackNum) const {
            return isValidTrack(trackNum) && tracks[trackNum].isPlaying && !tracks[trackNum].isPaused;
        }
        
        bool isPaused(int trackNum) const {
            return isValidTrack(trackNum) && tracks[trackNum].isPlaying && tracks[trackNum].isPaused;
        }
        
        void setVolume(int trackNum, float volume) {
            if (isValidTrack(trackNum)) {
                tracks[trackNum].volume = constrain(volume, 0.0f, 1.0f);
            }
        }
        
        float getVolume(int trackNum) const {
            return isValidTrack(trackNum) ? tracks[trackNum].volume : 0.0f;
        }
        
        bool tick() {
            if (!initialized) return false;
            
            memset(mixBuffer, 0, mixBufferSize * sizeof(int16_t));
            bool anyActive = false;
            
            for (int i = 0; i < MAX_TRACKS; i++) {
                if (tracks[i].isPlaying && !tracks[i].isPaused) {
                    if (mixTrack(i)) {
                        anyActive = true;
                    }
                }
            }
            
            if (anyActive) {
                size_t bytesWritten;
                i2s_write(I2S_NUM_0, mixBuffer, mixBufferSize * sizeof(int16_t), &bytesWritten, portMAX_DELAY);
            }
            
            return true;
        }

    private:
        bool isValidTrack(int trackNum) const {
            return trackNum >= 0 && trackNum < MAX_TRACKS && initialized;
        }
        
        bool mixTrack(int trackNum) {
            AudioTrack& track = tracks[trackNum];
            
            // Refill buffer if empty
            if (track.bufferPos >= mixBufferSize) {
                size_t samplesToRead = mixBufferSize;
                
                size_t bytesRead = track.stream->read(reinterpret_cast<char*>(track.buffer), samplesToRead * sizeof(int16_t));
                track.bufferPos = 0;
                
                if (bytesRead == 0) {
                    if(track.loop) {
                        track.stream->seek(44);
                         track.fadeVolume = 0;
                    }
                    else {
                        stop(trackNum);
                        return false;
                    }
                }
            }
            
            // Mix samples with volume
            for (size_t i = 0; i < mixBufferSize && track.bufferPos < mixBufferSize; i++) {
                mixBuffer[i] += track.buffer[track.bufferPos++] * track.fadeVolume;
            }

            if(track.fadeVolume < track.volume) {
                track.fadeVolume += 0.1f;
            }
            
            return true;
        }
    };
}