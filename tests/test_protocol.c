#include "../src/config.h"
#include "../src/protocol.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void test_vidpid_table(void) {
    const stm_vidpid_t *m = stm_match_vidpid(0x0145, 0x1001);
    assert(m != NULL);
    assert(strcmp(m->profile_name, "SystemTemperatureMonitoring") == 0);

    assert(stm_match_vidpid(0x12b7, 0x1001) != NULL);
    assert(stm_match_vidpid(0x1c57, 0x3ed1) != NULL);
    assert(stm_match_vidpid(0x0145, 0x1005) != NULL);
    assert(stm_match_vidpid(0xdead, 0xbeef) == NULL);
    printf("PASS: test_vidpid_table\n");
}

static void test_handshake_math(void) {
    /*
     * Deterministic seed verification against TempComm.dll 0x1800010f0:
     * K1 = 0x1234 (p[4]=0x12, p[5]=0x34)
     * K2 = 0xABCD (p[6]=0xAB, p[7]=0xCD)
     * K3 = 0x5678 (p[8]=0x56, p[9]=0x78)
     *
     * K2 + K3 mod 65536 = 0xABCD + 0x5678 = 0x10245 -> 0x0245
     * auth_out = 0x1234 ^ 0x0245 = 0x1071 -> p[10]=0x10, p[11]=0x71
     *
     * K1 + K2 mod 65536 = 0x1234 + 0xABCD = 0xBE01
     * auth_in  = 0x5678 ^ 0xBE01 = 0xE879
     */
    uint8_t seed[STM_PAYLOAD_BYTES];
    for (size_t i = 0; i < sizeof(seed); i++) {
        seed[i] = (uint8_t)(i + 1);
    }
    seed[4] = 0x12; seed[5] = 0x34;
    seed[6] = 0xAB; seed[7] = 0xCD;
    seed[8] = 0x56; seed[9] = 0x78;

    uint8_t out_report[STM_REPORT_BYTES];
    uint16_t exp_key = 0;
    stm_build_handshake(out_report, &exp_key, seed);

    assert(out_report[0] == 0x00);
    const uint8_t *p = &out_report[1];
    assert(p[0] == 0x01);
    assert(p[1] == 0x00 && p[2] == 0x00 && p[3] == 0x00);
    assert(p[4] == 0x12 && p[5] == 0x34);
    assert(p[6] == 0xAB && p[7] == 0xCD);
    assert(p[8] == 0x56 && p[9] == 0x78);
    assert(p[10] == 0x10 && p[11] == 0x71);
    assert(exp_key == 0xE879);

    /* Verify outgoing checksums independently */
    uint8_t x = 0, a = 0;
    for (unsigned int i = 0; i < 60; i++) x ^= (uint8_t)(i * p[i]);
    for (unsigned int i = 0; i < 63; i++) a = (uint8_t)(a + (uint8_t)(i * p[i]));
    assert(p[62] == x);
    assert(p[63] == a);

    /* Build valid simulated device reply */
    uint8_t reply[STM_PAYLOAD_BYTES];
    memset(reply, 0x42, sizeof(reply));
    reply[0] = 0x01;
    reply[4] = (uint8_t)(exp_key >> 8);
    reply[5] = (uint8_t)(exp_key & 0xFF);
    stm_compute_checksums(reply);
    assert(stm_verify_handshake_reply(reply, exp_key) == true);

    /* Corrupt key -> must reject */
    reply[4] ^= 0x01;
    stm_compute_checksums(reply);
    assert(stm_verify_handshake_reply(reply, exp_key) == false);

    /* Restore key and corrupt checksum -> must reject */
    reply[4] = (uint8_t)(exp_key >> 8);
    stm_compute_checksums(reply);
    reply[63] ^= 0x01;
    assert(stm_verify_handshake_reply(reply, exp_key) == false);

    printf("PASS: test_handshake_math\n");
}

static void test_telemetry_layout(void) {
    stm_telemetry_t t;
    memset(&t, 0, sizeof(t));
    t.cpu_temp_c = 62;
    t.gpu_temp_c = 54;
    t.hour = 22;
    t.minute = 45;
    t.second = 30;
    t.centisecond = 87;
    t.year = 2026;
    t.month = 11;
    t.day = 16;
    t.day_of_week = 1;
    t.cpu_load_pct = 37;
    t.cpu_clock_mhz = 4750; /* 0x128E */
    t.gpu_load_pct = 81;
    t.gpu_clock_mhz = 2400; /* 0x0960 */
    t.ram_temp_c = 39;
    t.ram_load_pct = 23;
    t.ram_clock_mhz = 6000; /* 0x1770 */
    t.disk_temp_c = 42;
    t.disk_load_pct = 15;
    t.cpu_fan_rpm = 1493;   /* 0x05D5 */
    t.cpu_voltage_v = 1.25;
    t.cpu_watt = 65.4;      /* 654 = 0x028E */
    t.gpu_fan = 55;         /* 0x0037 */
    t.gpu_voltage_v = 0.87;
    t.gpu_watt = 123.4;     /* 1234 = 0x04D2 */
    t.net_up_bps = 0x11223344u;
    t.net_down_bps = 0x55667788u;
    t.display_mode = stm_encode_display_mode(0, 3); /* 0x83 */

    uint8_t rpt[STM_REPORT_BYTES];
    stm_build_telemetry(&t, rpt);
    assert(rpt[0] == 0x00);

    const uint8_t *p = &rpt[1];
    assert(p[0] == 0x02);
    assert(p[1] == 62);
    assert(p[2] == 54);
    assert(p[3] == 22 && p[4] == 45 && p[5] == 30 && p[6] == 87);
    assert(p[7] == 20 && p[8] == 26 && p[9] == 11 && p[10] == 16 && p[11] == 1);
    assert(p[12] == 37);
    assert(p[13] == 0x12 && p[14] == 0x8E);
    assert(p[15] == 81);
    assert(p[16] == 0x09 && p[17] == 0x60);
    assert(p[18] == 39 && p[19] == 23);
    assert(p[20] == 0x17 && p[21] == 0x70);
    assert(p[22] == 42 && p[23] == 15);
    assert(p[24] == 0x05 && p[25] == 0xD5);
    assert(p[26] == 1 && p[27] == 25);
    assert(p[28] == 0x02 && p[29] == 0x8E);
    assert(p[30] == 0x00 && p[31] == 0x37);
    assert(p[32] == 0 && p[33] == 87);
    assert(p[34] == 0x04 && p[35] == 0xD2);
    assert(p[36] == 0 && p[37] == 0 && p[38] == 0 && p[39] == 0);
    assert(p[40] == 0x11 && p[41] == 0x22 && p[42] == 0x33 && p[43] == 0x44);
    assert(p[44] == 0x55 && p[45] == 0x66 && p[46] == 0x77 && p[47] == 0x88);
    assert(p[48] == 0x83);
    for (size_t i = 49; i < STM_PAYLOAD_BYTES; i++) {
        assert(p[i] == 0x00);
    }
    assert(stm_encode_display_mode(1, 3) == 0x03);
    printf("PASS: test_telemetry_layout\n");
}

static void test_config_roundtrip(void) {
    stm_config_t a, b;
    stm_config_defaults(&a);
    a.refresh_delay_ms = 250;
    a.send_delay_ms = 750;
    a.is_log = true;
    a.local_port = 8899;
    a.display_mode = 0;
    a.display_submode = 2;
    a.gpu_fan_mode = STM_GPU_FAN_RPM;
    snprintf(a.cpu_fan_channel, sizeof(a.cpu_fan_channel), "fan2");

    const char *tmp = "/tmp/stm_test_config.ini";
    assert(stm_config_save(&a, tmp));
    stm_config_defaults(&b);
    assert(stm_config_load(&b, tmp));
    remove(tmp);

    assert(b.refresh_delay_ms == 250);
    assert(b.send_delay_ms == 750);
    assert(b.is_log == true);
    assert(b.local_port == 8899);
    assert(b.display_mode == 0);
    assert(b.display_submode == 2);
    assert(b.gpu_fan_mode == STM_GPU_FAN_RPM);
    assert(strcmp(b.cpu_fan_channel, "fan2") == 0);
    printf("PASS: test_config_roundtrip\n");
}

int main(void) {
    test_vidpid_table();
    test_handshake_math();
    test_telemetry_layout();
    test_config_roundtrip();
    printf("ALL TESTS PASSED\n");
    return 0;
}
