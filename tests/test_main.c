/*
 * USBMIDI9 host test runner.
 *
 * Portable C (C89/C90). Returns 0 when all tests pass, 1 otherwise.
 */

#include <stdio.h>

int test_packets_run(void);
int test_descriptors_run(void);
int test_ring_run(void);
int test_machine_run(void);
int test_probe_run(void);

int main(void)
{
    int failures;

    failures = 0;
    failures += test_packets_run();
    failures += test_descriptors_run();
    failures += test_ring_run();
    failures += test_machine_run();
    failures += test_probe_run();

    if (failures != 0) {
        printf("FAILED: %d check(s) failed\n", failures);
        return 1;
    }
    printf("All tests passed.\n");
    return 0;
}
