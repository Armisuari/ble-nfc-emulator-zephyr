#pragma once

#include <stdint.h>

struct device;

class NfcReaderPn532
{
    public:
        NfcReaderPn532();
        int init();
        int start();
        void stop();
        bool is_ready() const;
        int last_error() const;
        void process_once();
        ~NfcReaderPn532() = default;

    private:
        const struct device *i2c_bus_;
        bool ready_;
        bool running_;
        bool last_detected_;
        uint8_t last_uid_[7];
        uint8_t last_uid_len_;
        uint8_t detect_streak_;
        uint8_t miss_streak_;
        int last_error_;

        int read_passive_target_uid(uint8_t *uid, uint8_t *uid_len);
        int publish_nfc_event(const uint8_t *uid, uint8_t uid_len, bool detected);
};