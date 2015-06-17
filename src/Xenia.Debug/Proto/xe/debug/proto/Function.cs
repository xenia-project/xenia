// automatically generated, do not modify

namespace xe.debug.proto
{

using FlatBuffers;

public sealed class Function : Table {
  public static Function GetRootAsFunction(ByteBuffer _bb) { return GetRootAsFunction(_bb, new Function()); }
  public static Function GetRootAsFunction(ByteBuffer _bb, Function obj) { return (obj.__init(_bb.GetInt(_bb.Position) + _bb.Position, _bb)); }
  public Function __init(int _i, ByteBuffer _bb) { bb_pos = _i; bb = _bb; return this; }

  public ulong Identifier { get { int o = __offset(4); return o != 0 ? bb.GetUlong(o + bb_pos) : (ulong)0; } }
  public uint AddressStart { get { int o = __offset(6); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }
  public uint AddressEnd { get { int o = __offset(8); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }
  public string Name { get { int o = __offset(10); return o != 0 ? __string(o + bb_pos) : null; } }
  public uint MachineCodeStart { get { int o = __offset(12); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }
  public uint MachineCodeEnd { get { int o = __offset(14); return o != 0 ? bb.GetUint(o + bb_pos) : (uint)0; } }
  public string DisasmHirRaw { get { int o = __offset(16); return o != 0 ? __string(o + bb_pos) : null; } }
  public string DisasmHirOpt { get { int o = __offset(18); return o != 0 ? __string(o + bb_pos) : null; } }

  public static int CreateFunction(FlatBufferBuilder builder,
      ulong identifier = 0,
      uint address_start = 0,
      uint address_end = 0,
      int name = 0,
      uint machine_code_start = 0,
      uint machine_code_end = 0,
      int disasm_hir_raw = 0,
      int disasm_hir_opt = 0) {
    builder.StartObject(8);
    Function.AddIdentifier(builder, identifier);
    Function.AddDisasmHirOpt(builder, disasm_hir_opt);
    Function.AddDisasmHirRaw(builder, disasm_hir_raw);
    Function.AddMachineCodeEnd(builder, machine_code_end);
    Function.AddMachineCodeStart(builder, machine_code_start);
    Function.AddName(builder, name);
    Function.AddAddressEnd(builder, address_end);
    Function.AddAddressStart(builder, address_start);
    return Function.EndFunction(builder);
  }

  public static void StartFunction(FlatBufferBuilder builder) { builder.StartObject(8); }
  public static void AddIdentifier(FlatBufferBuilder builder, ulong identifier) { builder.AddUlong(0, identifier, 0); }
  public static void AddAddressStart(FlatBufferBuilder builder, uint addressStart) { builder.AddUint(1, addressStart, 0); }
  public static void AddAddressEnd(FlatBufferBuilder builder, uint addressEnd) { builder.AddUint(2, addressEnd, 0); }
  public static void AddName(FlatBufferBuilder builder, int nameOffset) { builder.AddOffset(3, nameOffset, 0); }
  public static void AddMachineCodeStart(FlatBufferBuilder builder, uint machineCodeStart) { builder.AddUint(4, machineCodeStart, 0); }
  public static void AddMachineCodeEnd(FlatBufferBuilder builder, uint machineCodeEnd) { builder.AddUint(5, machineCodeEnd, 0); }
  public static void AddDisasmHirRaw(FlatBufferBuilder builder, int disasmHirRawOffset) { builder.AddOffset(6, disasmHirRawOffset, 0); }
  public static void AddDisasmHirOpt(FlatBufferBuilder builder, int disasmHirOptOffset) { builder.AddOffset(7, disasmHirOptOffset, 0); }
  public static int EndFunction(FlatBufferBuilder builder) {
    int o = builder.EndObject();
    return o;
  }
};


}
