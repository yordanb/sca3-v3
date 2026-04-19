#include "fault_persist.h"
#include "nvs.h"

static const char* NS_NAME = "fault";

bool faultPersistSave(const fault_persist_record_t& rec) {
    nvs_handle_t nvs;
    if (nvs_open(NS_NAME, NVS_READWRITE, &nvs) != ESP_OK) return false;
    bool ok = true;
    ok &= (nvs_set_u8(nvs, "valid", rec.valid ? 0xA5 : 0x00) == ESP_OK);
    ok &= (nvs_set_u8(nvs, "code", rec.code) == ESP_OK);
    ok &= (nvs_set_u8(nvs, "scope", rec.scope) == ESP_OK);
    ok &= (nvs_set_i8(nvs, "slot", rec.slot) == ESP_OK);
    ok &= (nvs_set_u32(nvs, "ts", rec.timestamp) == ESP_OK);
    ok &= (nvs_commit(nvs) == ESP_OK);
    nvs_close(nvs);
    return ok;
}

bool faultPersistLoad(fault_persist_record_t* out) {
    if (!out) return false;
    nvs_handle_t nvs;
    uint8_t valid = 0;
    if (nvs_open(NS_NAME, NVS_READONLY, &nvs) != ESP_OK) return false;
    if (nvs_get_u8(nvs, "valid", &valid) != ESP_OK || valid != 0xA5) {
        nvs_close(nvs);
        return false;
    }
    out->valid = true;
    nvs_get_u8(nvs, "code", &out->code);
    nvs_get_u8(nvs, "scope", &out->scope);
    nvs_get_i8(nvs, "slot", &out->slot);
    nvs_get_u32(nvs, "ts", &out->timestamp);
    nvs_close(nvs);
    return true;
}

bool faultPersistClear() {
    nvs_handle_t nvs;
    if (nvs_open(NS_NAME, NVS_READWRITE, &nvs) != ESP_OK) return false;
    bool ok = (nvs_erase_all(nvs) == ESP_OK) && (nvs_commit(nvs) == ESP_OK);
    nvs_close(nvs);
    return ok;
}
