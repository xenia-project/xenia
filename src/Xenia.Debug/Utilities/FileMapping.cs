using Microsoft.Win32.SafeHandles;
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Linq;
using System.Runtime.ConstrainedExecution;
using System.Runtime.InteropServices;
using System.Security;
using System.Security.Permissions;
using System.Text;
using System.Threading.Tasks;

namespace Xenia.Debug.Utilities {
  // From: http://1code.codeplex.com/SourceControl/latest#Visual Studio 2008/CSFileMappingClient/Program.cs

  /// <summary>
  /// Access rights for file mapping objects
  /// http://msdn.microsoft.com/en-us/library/aa366559.aspx
  /// </summary>
  [Flags]
  public enum FileMapAccess {
    FILE_MAP_COPY = 0x0001,
    FILE_MAP_WRITE = 0x0002,
    FILE_MAP_READ = 0x0004,
    FILE_MAP_ALL_ACCESS = 0x000F001F
  }

  /// <summary>
  /// Represents a wrapper class for a file mapping handle.
  /// </summary>
  [SuppressUnmanagedCodeSecurity,
  HostProtection(SecurityAction.LinkDemand, MayLeakOnAbort = true)]
  public sealed class FileMappingHandle : SafeHandleZeroOrMinusOneIsInvalid {
    [SecurityPermission(SecurityAction.LinkDemand, UnmanagedCode = true)]
    private FileMappingHandle()
        : base(true) {
    }

    [SecurityPermission(SecurityAction.LinkDemand, UnmanagedCode = true)]
    public FileMappingHandle(IntPtr handle, bool ownsHandle)
        : base(ownsHandle) {
      base.SetHandle(handle);
    }

    [ReliabilityContract(Consistency.WillNotCorruptState, Cer.Success),
    DllImport("kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool CloseHandle(IntPtr handle);

    protected override bool ReleaseHandle() {
      return CloseHandle(base.handle);
    }
  }


  /// <summary>
  /// The class exposes Windows APIs used in this code sample.
  /// </summary>
  [SuppressUnmanagedCodeSecurity]
  public class FileMapping {
    /// <summary>
    /// Opens a named file mapping object.
    /// </summary>
    /// <param name="dwDesiredAccess">
    /// The access to the file mapping object. This access is checked against 
    /// any security descriptor on the target file mapping object.
    /// </param>
    /// <param name="bInheritHandle">
    /// If this parameter is TRUE, a process created by the CreateProcess 
    /// function can inherit the handle; otherwise, the handle cannot be 
    /// inherited.
    /// </param>
    /// <param name="lpName">
    /// The name of the file mapping object to be opened.
    /// </param>
    /// <returns>
    /// If the function succeeds, the return value is an open handle to the 
    /// specified file mapping object.
    /// </returns>
    [DllImport("kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
    public static extern FileMappingHandle OpenFileMapping(
        FileMapAccess dwDesiredAccess, bool bInheritHandle, string lpName);


    /// <summary>
    /// Maps a view of a file mapping into the address space of a calling
    /// process.
    /// </summary>
    /// <param name="hFileMappingObject">
    /// A handle to a file mapping object. The CreateFileMapping and 
    /// OpenFileMapping functions return this handle.
    /// </param>
    /// <param name="dwDesiredAccess">
    /// The type of access to a file mapping object, which determines the 
    /// protection of the pages.
    /// </param>
    /// <param name="dwFileOffsetHigh">
    /// A high-order DWORD of the file offset where the view begins.
    /// </param>
    /// <param name="dwFileOffsetLow">
    /// A low-order DWORD of the file offset where the view is to begin.
    /// </param>
    /// <param name="dwNumberOfBytesToMap">
    /// The number of bytes of a file mapping to map to the view. All bytes 
    /// must be within the maximum size specified by CreateFileMapping.
    /// </param>
    /// <returns>
    /// If the function succeeds, the return value is the starting address 
    /// of the mapped view.
    /// </returns>
    [DllImport("Kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
    public static extern IntPtr MapViewOfFile(
        FileMappingHandle hFileMappingObject,
        FileMapAccess dwDesiredAccess,
        uint dwFileOffsetHigh,
        uint dwFileOffsetLow,
        ulong dwNumberOfBytesToMap);

    [DllImport("Kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
    public static extern IntPtr MapViewOfFileEx(
        FileMappingHandle hFileMappingObject,
        FileMapAccess dwDesiredAccess,
        uint dwFileOffsetHigh,
        uint dwFileOffsetLow,
        ulong dwNumberOfBytesToMap,
        ulong lpBaseAddress);


    /// <summary>
    /// Unmaps a mapped view of a file from the calling process's address 
    /// space.
    /// </summary>
    /// <param name="lpBaseAddress">
    /// A pointer to the base address of the mapped view of a file that 
    /// is to be unmapped.
    /// </param>
    /// <returns></returns>
    [DllImport("Kernel32.dll", CharSet = CharSet.Auto, SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool UnmapViewOfFile(IntPtr lpBaseAddress);
  }
}
