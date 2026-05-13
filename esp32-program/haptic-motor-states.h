#define HAPTIC_MOTOR_COUNT 4
#define HAPTIC_STATE_BITS 2
#define HAPTIC_STATE_BIT_MASK ((1 << HAPTIC_STATE_BITS) - 1)

typedef enum {
    Haptic_Stop = 0,
    Haptic_Slow = 1,
    Haptic_Fast = 2,
} Haptic_Motor_State;

typedef unsigned char Haptic_States;

_Static_assert(HAPTIC_MOTOR_COUNT*HAPTIC_STATE_BITS <= sizeof(Haptic_States) * 8, "motor state bits not fit");

Haptic_States haptic_motor_pack_states(Haptic_Motor_State s0, Haptic_Motor_State s1, Haptic_Motor_State s2, Haptic_Motor_State s3) {
    return
        (s0 << (0 * HAPTIC_STATE_BITS)) |
        (s1 << (1 * HAPTIC_STATE_BITS)) |
        (s2 << (2 * HAPTIC_STATE_BITS)) |
        (s3 << (3 * HAPTIC_STATE_BITS));
}
