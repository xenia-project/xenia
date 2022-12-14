

#if defined(OVERRIDING_BASE_CMDPROCESSOR)
#define	PM4_OVERRIDE		override
#else
#define PM4_OVERRIDE
#endif
void ExecuteIndirectBuffer(uint32_t ptr,
                           uint32_t count) XE_RESTRICT;
virtual uint32_t ExecutePrimaryBuffer(uint32_t start_index, uint32_t end_index)
    XE_RESTRICT PM4_OVERRIDE;
virtual bool ExecutePacket() PM4_OVERRIDE;

public:
void ExecutePacket(uint32_t ptr, uint32_t count);

protected:
 XE_NOINLINE
bool ExecutePacketType0( uint32_t packet) XE_RESTRICT;
XE_NOINLINE
bool ExecutePacketType1( uint32_t packet) XE_RESTRICT;

bool ExecutePacketType2( uint32_t packet) XE_RESTRICT;
XE_NOINLINE
bool ExecutePacketType3( uint32_t packet) XE_RESTRICT;
XE_NOINLINE
bool ExecutePacketType3_ME_INIT( uint32_t packet,
                                uint32_t count) XE_RESTRICT;
bool ExecutePacketType3_NOP( uint32_t packet,
                            uint32_t count) XE_RESTRICT;
XE_NOINLINE
bool ExecutePacketType3_INTERRUPT( uint32_t packet,
                                  uint32_t count) XE_RESTRICT;
XE_NOINLINE
bool ExecutePacketType3_XE_SWAP( uint32_t packet,
                                uint32_t count) XE_RESTRICT;

bool ExecutePacketType3_INDIRECT_BUFFER( uint32_t packet,
                                        uint32_t count) XE_RESTRICT;
XE_NOINLINE
bool ExecutePacketType3_WAIT_REG_MEM( uint32_t packet,
                                     uint32_t count) XE_RESTRICT;
XE_NOINLINE
bool ExecutePacketType3_REG_RMW( uint32_t packet,
                                uint32_t count) XE_RESTRICT;

bool ExecutePacketType3_REG_TO_MEM( uint32_t packet,
                                   uint32_t count) XE_RESTRICT;
XE_NOINLINE
bool ExecutePacketType3_MEM_WRITE( uint32_t packet,
                                  uint32_t count) XE_RESTRICT;
XE_NOINLINE
bool ExecutePacketType3_COND_WRITE( uint32_t packet,
                                   uint32_t count) XE_RESTRICT;

bool ExecutePacketType3_EVENT_WRITE( uint32_t packet,
                                    uint32_t count) XE_RESTRICT;
XE_NOINLINE
bool ExecutePacketType3_EVENT_WRITE_SHD( uint32_t packet,
                                        uint32_t count) XE_RESTRICT;

bool ExecutePacketType3_EVENT_WRITE_EXT( uint32_t packet,
                                        uint32_t count) XE_RESTRICT;
XE_NOINLINE
bool ExecutePacketType3_EVENT_WRITE_ZPD( uint32_t packet,
                                        uint32_t count) XE_RESTRICT;

bool ExecutePacketType3Draw( uint32_t packet,
                            const char* opcode_name,
                            uint32_t viz_query_condition,
                            uint32_t count_remaining) XE_RESTRICT;

bool ExecutePacketType3_DRAW_INDX( uint32_t packet,
                                  uint32_t count) XE_RESTRICT;

bool ExecutePacketType3_DRAW_INDX_2( uint32_t packet,
                                    uint32_t count) XE_RESTRICT;
XE_FORCEINLINE
bool ExecutePacketType3_SET_CONSTANT( uint32_t packet,
                                     uint32_t count) XE_RESTRICT;
XE_NOINLINE
bool ExecutePacketType3_SET_CONSTANT2( uint32_t packet,
                                      uint32_t count) XE_RESTRICT;
XE_FORCEINLINE
bool ExecutePacketType3_LOAD_ALU_CONSTANT( uint32_t packet,
                                          uint32_t count) XE_RESTRICT;

bool ExecutePacketType3_SET_SHADER_CONSTANTS(
                                             uint32_t packet,
                                             uint32_t count) XE_RESTRICT;

bool ExecutePacketType3_IM_LOAD( uint32_t packet,
                                uint32_t count) XE_RESTRICT;

bool ExecutePacketType3_IM_LOAD_IMMEDIATE( uint32_t packet,
                                          uint32_t count) XE_RESTRICT;

bool ExecutePacketType3_INVALIDATE_STATE( uint32_t packet,
                                         uint32_t count) XE_RESTRICT;

bool ExecutePacketType3_VIZ_QUERY( uint32_t packet,
                                  uint32_t count) XE_RESTRICT;


XE_FORCEINLINE
void WriteEventInitiator(uint32_t value) XE_RESTRICT;

XE_NOINLINE
XE_COLD
bool HitUnimplementedOpcode(uint32_t opcode, uint32_t count) XE_RESTRICT;

XE_FORCEINLINE
XE_NOALIAS
uint32_t GetCurrentRingReadCount();

XE_NOINLINE
XE_COLD
bool ExecutePacketType3_CountOverflow(uint32_t count);
XE_NOINLINE
XE_COLD
bool ExecutePacketType0_CountOverflow(uint32_t count);

#undef PM4_OVERRIDE