// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class FunctionEntry : Table {
  public static FunctionEntry GetRootAsFunctionEntry(ByteBuffer _bb) { return GetRootAsFunctionEntry(_bb, new FunctionEntry()); }
  public static FunctionEntry GetRootAsFunctionEntry(ByteBuffer _bb, FunctionEntry obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public FunctionEntry __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }

  public ulong Identifier { get { int o = __offset(4); return o != 0 ? bb.GetUlong(o + bb_pos) : (ulong)0; } }
  public uint AddressStart { get { int o = __offset(6); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }
  public uint AddressEnd { get { int o = __offset(8); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }
  public string Name { get { int o = __offset(10); return o != 0 ? __string(o + bb_pos) : null; } }

  public static int CreateFunctionEntry(FlatBufferBuilder builder,
      ulong identifier = 0,
      uint address_start = 0,
      uint address_end = 0,
      int name = 0) {
    builder.StartObject(4);
    FunctionEntry.AddIdentifier(builder, identifier);
    FunctionEntry.AddName(builder, name);
    FunctionEntry.AddAddressEnd(builder, address_end);
    FunctionEntry.AddAddressStart(builder, address_start);
    return FunctionEntry.EndFunctionEntry(builder);
  }

  public static void StartFunctionEntry(FlatBufferBuilder builder) { builder.StartObject(4); }
  public static void AddIdentifier(FlatBufferBuilder builder, ulong identifier) { builder.AddUlong(0, identifier, 0); }
  public static void AddAddressStart(FlatBufferBuilder builder, uint addressStart) { builder.AddUint(1, addressStart, 0); }
  public static void AddAddressEnd(FlatBufferBuilder builder, uint addressEnd) { builder.AddUint(2, addressEnd, 0); }
  public static void AddName(FlatBufferBuilder builder, int nameOffset) { builder.AddOffset(3, nameOffset, 0); }
  public static int EndFunctionEntry(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
