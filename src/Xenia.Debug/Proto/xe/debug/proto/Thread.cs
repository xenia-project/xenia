// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class Thread : Table {
  public static Thread GetRootAsThread(ByteBuffer _bb) { return GetRootAsThread(_bb, new Thread()); }
  public static Thread GetRootAsThread(ByteBuffer _bb, Thread obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public Thread __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }

  public XObject Object { get { return GetObject(new XObject()); } }
  public XObject GetObject(XObject obj) { int o = __offset(4); return o != 0 ? obj.__init(o + bb_pos, bb) : null; }
  public ThreadType Type { get { int o = __offset(6); return o != 0 ? (ThreadType)bb.GetSbyte(o + bb_pos) : (ThreadType)0; } }
  public uint StackSize { get { int o = __offset(8); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }
  public uint XapiThreadStartup { get { int o = __offset(10); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }
  public uint StartAddress { get { int o = __offset(12); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }
  public uint StartContext { get { int o = __offset(14); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }
  public uint CreationFlags { get { int o = __offset(16); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }
  public uint TlsAddress { get { int o = __offset(18); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }
  public uint PcrAddress { get { int o = __offset(20); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }
  public uint ThreadStateAddress { get { int o = __offset(22); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }
  public uint ThreadId { get { int o = __offset(24); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }
  public string Name { get { int o = __offset(26); return o != 0 ? __string(o + bb_pos) : null; } }
  public uint Priority { get { int o = __offset(28); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }
  public uint Affinity { get { int o = __offset(30); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }
  public uint State { get { int o = __offset(32); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }

  public static void StartThread(FlatBufferBuilder builder) { builder.StartObject(15); }
  public static void AddObject(FlatBufferBuilder builder, int objectOffset) { builder.AddStruct(0, objectOffset, 0); }
  public static void AddType(FlatBufferBuilder builder, ThreadType type) { builder.AddSbyte(1, (sbyte)(type), 0); }
  public static void AddStackSize(FlatBufferBuilder builder, uint stackSize) { builder.AddUint(2, stackSize, 0); }
  public static void AddXapiThreadStartup(FlatBufferBuilder builder, uint xapiThreadStartup) { builder.AddUint(3, xapiThreadStartup, 0); }
  public static void AddStartAddress(FlatBufferBuilder builder, uint startAddress) { builder.AddUint(4, startAddress, 0); }
  public static void AddStartContext(FlatBufferBuilder builder, uint startContext) { builder.AddUint(5, startContext, 0); }
  public static void AddCreationFlags(FlatBufferBuilder builder, uint creationFlags) { builder.AddUint(6, creationFlags, 0); }
  public static void AddTlsAddress(FlatBufferBuilder builder, uint tlsAddress) { builder.AddUint(7, tlsAddress, 0); }
  public static void AddPcrAddress(FlatBufferBuilder builder, uint pcrAddress) { builder.AddUint(8, pcrAddress, 0); }
  public static void AddThreadStateAddress(FlatBufferBuilder builder, uint threadStateAddress) { builder.AddUint(9, threadStateAddress, 0); }
  public static void AddThreadId(FlatBufferBuilder builder, uint threadId) { builder.AddUint(10, threadId, 0); }
  public static void AddName(FlatBufferBuilder builder, int nameOffset) { builder.AddOffset(11, nameOffset, 0); }
  public static void AddPriority(FlatBufferBuilder builder, uint priority) { builder.AddUint(12, priority, 0); }
  public static void AddAffinity(FlatBufferBuilder builder, uint affinity) { builder.AddUint(13, affinity, 0); }
  public static void AddState(FlatBufferBuilder builder, uint state) { builder.AddUint(14, state, 0); }
  public static int EndThread(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
