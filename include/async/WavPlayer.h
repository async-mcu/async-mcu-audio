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
            uint32_t dataSize;  // 0 = infinite stream
            uint32_t position;
            float volume;
            int16_t* buffer;
            size_t bufferPos;
            bool parseWavHeader;
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
                    .dataSize = 0,
                    .position = 0,
                    .volume = 1.0f,
                    .buffer = nullptr,
                    .bufferPos = 0,
                    .parseWavHeader = false
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
                .dma_buf_len = 512,
                .use_apll = false,
                .tx_desc_auto_clear = true
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
        
        bool play(int trackNum, Stream* stream, bool parseWavHeader = false, uint32_t dataSize = 0) {
            if (trackNum < 0 || trackNum >= MAX_TRACKS || !initialized || !stream) return false;
            
            //stop(trackNum);
            
            uint32_t headerSize = 0;
            if (parseWavHeader) {
                if (!skipWavHeader(stream)) {
                    return false;
                }
                headerSize = 44; // Standard WAV header size for 16-bit mono
            }
            
            tracks[trackNum] = {
                .stream = stream,
                .isPlaying = true,
                .isPaused = false,
                .dataSize = dataSize > headerSize ? dataSize - headerSize : 0,
                .position = 0,
                .volume = tracks[trackNum].volume,
                .buffer = tracks[trackNum].buffer,
                .bufferPos = mixBufferSize, // Force buffer refill
                .parseWavHeader = parseWavHeader
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
        
        bool skipWavHeader(Stream* stream) {
            // Check RIFF header
            char header[12];
            if (stream->read(header, 12) != 12) return false;
            if (strncmp(header, "RIFF", 4) != 0 || strncmp(header+8, "WAVE", 4) != 0) return false;
            
            // Find "fmt " chunk
            while (true) {
                char chunkHeader[8];
                if (stream->read(chunkHeader, 8) != 8) return false;
                
                uint32_t chunkSize = *reinterpret_cast<uint32_t*>(chunkHeader + 4);
                if (strncmp(chunkHeader, "fmt ", 4) == 0) {
                    // Skip format chunk (we assume it's 16-bit mono)
                    stream->seek(stream->position() + chunkSize);
                    break;
                } else {
                    stream->seek(stream->position() + chunkSize);
                }
            }
            
            // Find "data" chunk
            while (true) {
                char chunkHeader[8];
                if (stream->read(chunkHeader, 8) != 8) return false;
                
                if (strncmp(chunkHeader, "data", 4) == 0) {
                    return true;
                } else {
                    uint32_t chunkSize = *reinterpret_cast<uint32_t*>(chunkHeader + 4);
                    stream->seek(stream->position() + chunkSize);
                }
            }
        }
        
        bool mixTrack(int trackNum) {
            AudioTrack& track = tracks[trackNum];
            
            // Refill buffer if empty
            if (track.bufferPos >= mixBufferSize) {
                size_t samplesToRead = mixBufferSize;
                
                // For streams with known size
                if (track.dataSize > 0) {
                    size_t bytesRemaining = track.dataSize - track.position;
                    samplesToRead = min(mixBufferSize, bytesRemaining / sizeof(int16_t));
                    
                    if (samplesToRead == 0) {
                        stop(trackNum);
                        return false;
                    }
                }
                
                size_t bytesRead = track.stream->read(reinterpret_cast<char*>(track.buffer), samplesToRead * sizeof(int16_t));
                track.bufferPos = 0;
                track.position += bytesRead;
                
                if (bytesRead == 0) {
                    stop(trackNum);
                    return false;
                }
            }
            
            // Mix samples with volume
            for (size_t i = 0; i < mixBufferSize && track.bufferPos < mixBufferSize; i++) {
                mixBuffer[i] += track.buffer[track.bufferPos++] * track.volume;
            }
            
            return true;
        }
    };
}