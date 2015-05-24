// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class AccessViolationEvent : Table {
  public static AccessViolationEvent GetRootAsAccessViolationEvent(ByteBuffer _bb) { return GetRootAsAccessViolationEvent(_bb, new AccessViolationEvent()); }
  public static AccessViolationEvent GetRootAsAccessViolationEvent(ByteBuffer _bb, AccessViolationEvent obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public AccessViolationEvent __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }

  public uint ThreadId { get { int o = __offset(4); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }
  public uint TargetAddress { get { int o = __offset(6); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }

  public static int CreateAccessViolationEvent(FlatBufferBuilder builder,
      uint thread_id = 0,
      uint target_address = 0) {
    builder.StartObject(2);
    AccessViolationEvent.AddTargetAddress(builder, target_address);
    AccessViolationEvent.AddThreadId(builder, thread_id);
    return AccessViolationEvent.EndAccessViolationEvent(builder);
  }

  public static void StartAccessViolationEvent(FlatBufferBuilder builder) { builder.StartObject(2); }
  public static void AddThreadId(FlatBufferBuilder builder, uint threadId) { builder.AddUint(0, threadId, 0); }
  public static void AddTargetAddress(FlatBufferBuilder builder, uint targetAddress) { builder.AddUint(1, targetAddress, 0); }
  public static int EndAccessViolationEvent(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
