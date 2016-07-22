#ifndef UUID_6fd8e7d2_5ff5_4908_8b28_fbe474a74ebd
#define UUID_6fd8e7d2_5ff5_4908_8b28_fbe474a74ebd

enum class TriggerCondition
{
    NIM1,
    Scaler,
    Interrupt
};

enum class DAQState
{
    Idle,
    Starting,
    Running,
    Stopping
};

Q_DECLARE_METATYPE(DAQState);

#endif
