using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Xenia.Debug.Utilities;

namespace Xenia.Debug {
  public class Memory : Changeable, IDisposable {
    private readonly Debugger debugger;

    private class MapInfo {
      public ulong virtualAddressStart;
      public ulong virtualAddressEnd;
      public ulong targetAddress;
      public IntPtr ptr;
    }
    private MapInfo[] mapInfos = new MapInfo[]{
      // From memory.cc:
      new MapInfo(){virtualAddressStart = 0x00000000, virtualAddressEnd = 0x3FFFFFFF,
                    targetAddress = 0x0000000000000000},
      new MapInfo(){virtualAddressStart = 0x40000000, virtualAddressEnd = 0x7EFFFFFF,
                    targetAddress = 0x0000000040000000},
      new MapInfo(){virtualAddressStart = 0x7F000000, virtualAddressEnd = 0x7FFFFFFF,
                    targetAddress = 0x0000000100000000},
      new MapInfo(){virtualAddressStart = 0x80000000, virtualAddressEnd = 0x8FFFFFFF,
                    targetAddress = 0x0000000080000000},
      new MapInfo(){virtualAddressStart = 0x90000000, virtualAddressEnd = 0x9FFFFFFF,
                    targetAddress = 0x0000000080000000},
      new MapInfo(){virtualAddressStart = 0xA0000000, virtualAddressEnd = 0xBFFFFFFF,
                    targetAddress = 0x0000000100000000},
      new MapInfo(){virtualAddressStart = 0xC0000000, virtualAddressEnd = 0xDFFFFFFF,
                    targetAddress = 0x0000000100000000},
      new MapInfo(){virtualAddressStart = 0xE0000000, virtualAddressEnd = 0xFFFFFFFF,
                    targetAddress = 0x0000000100000000},
      new MapInfo(){virtualAddressStart = 0x100000000, virtualAddressEnd = 0x11FFFFFFF,
                    targetAddress = 0x0000000100000000},
    };

    public UIntPtr VirtualMembase;
    public UIntPtr PhysicalMembase;

    public Memory(Debugger debugger) {
      this.debugger = debugger;
    }

    public bool InitializeMapping(FileMappingHandle mappingHandle) {
      ulong mappingBase = 0x100000000;
      foreach (MapInfo mapInfo in mapInfos) {
        uint targetAddressLow = (uint)mapInfo.targetAddress;
        uint targetAddressHigh = (uint)(mapInfo.targetAddress >> 32);
        mapInfo.ptr = FileMapping.MapViewOfFileEx(
            mappingHandle, FileMapAccess.FILE_MAP_ALL_ACCESS,
            targetAddressHigh, targetAddressLow,
            mapInfo.virtualAddressEnd - mapInfo.virtualAddressStart + 1,
            mappingBase + mapInfo.virtualAddressStart);
        if (mapInfo.ptr == IntPtr.Zero) {
          System.Diagnostics.Debug.Fail("Unable to place memory at target address");
          return false;
        }
      }
      VirtualMembase = new UIntPtr(mappingBase);
      PhysicalMembase = new UIntPtr(mappingBase + 0x100000000);
      return true;
    }

    public void UninitializeMapping() {
      foreach (MapInfo mapInfo in mapInfos) {
        FileMapping.UnmapViewOfFile(mapInfo.ptr);
        mapInfo.ptr = IntPtr.Zero;
      }
    }

    private void OnDispose() {
      UninitializeMapping();
    }

    public MemoryView CreateView() {
      return new MemoryView(this);
    }

    #region Dispose
    private bool disposed = false;

    ~Memory() {
      Dispose(false);
    }

    public void Dispose() {
      Dispose(true);
      GC.SuppressFinalize(this);
    }

    protected virtual void Dispose(bool disposing) {
      if (!disposed) {
        if (disposing) {
          // TODO: dispose managed state (managed objects).
        }
        OnDispose();
        disposed = true;
      }
    }
    #endregion
  }
}
