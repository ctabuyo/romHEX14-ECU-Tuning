// checksumhelper.exe — 32-bit bridge for WinOLS checksum DLLs
// Usage: checksumhelper.exe <dll_path> <input_file> <output_file> <opcode>
// Opcodes: 102=verify, 103=correct
// Exit: 0=OK, 1=mismatch/fail, 2=DLL error, 3=IO error, 4=bad args

using System;
using System.IO;
using System.Runtime.InteropServices;

class Program
{
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Auto)]
    static extern IntPtr LoadLibrary(string lpFileName);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Auto)]
    static extern IntPtr LoadLibraryEx(string lpFileName, IntPtr hFile, uint dwFlags);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr GetProcAddress(IntPtr hModule, string lpProcName);

    [DllImport("kernel32.dll")]
    static extern bool FreeLibrary(IntPtr hModule);

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Auto)]
    static extern bool SetDllDirectory(string lpPathName);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern IntPtr CreateActCtx(ref ACTCTX pActCtx);

    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool ActivateActCtx(IntPtr hActCtx, out IntPtr lpCookie);

    [DllImport("kernel32.dll")]
    static extern bool DeactivateActCtx(uint dwFlags, IntPtr ulCookie);

    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Auto)]
    struct ACTCTX
    {
        public int cbSize;
        public uint dwFlags;
        public string lpSource;
        public ushort wProcessorArchitecture;
        public ushort wLangId;
        public string lpAssemblyDirectory;
        public string lpResourceName;
        public string lpApplicationName;
        public IntPtr hModule;
    }

    const uint ACTCTX_FLAG_ASSEMBLY_DIRECTORY_VALID = 0x04;

    // DefDLLProc signature: int __stdcall DefDLLProc(byte* data, uint size, uint opcode)
    [UnmanagedFunctionPointer(CallingConvention.StdCall)]
    delegate int DefDLLProcDelegate(IntPtr data, uint size, uint opcode);

    static int Main(string[] args)
    {
        if (args.Length < 4)
        {
            Console.WriteLine("{\"error\":\"usage: checksumhelper <dll> <input> <output> <opcode>\",\"code\":4}");
            return 4;
        }

        string dllPath = args[0];
        string inputFile = args[1];
        string outputFile = args[2];
        uint opcode;
        if (!uint.TryParse(args[3], out opcode))
        {
            Console.WriteLine("{\"error\":\"invalid opcode\",\"code\":4}");
            return 4;
        }

        // Set DLL search directory to the folder containing the DLL
        string dllDir = System.IO.Path.GetDirectoryName(System.IO.Path.GetFullPath(dllPath));
        if (!string.IsNullOrEmpty(dllDir))
        {
            SetDllDirectory(dllDir);
            Environment.SetEnvironmentVariable("PATH", dllDir + ";" + Environment.GetEnvironmentVariable("PATH"));
        }

        // Create an activation context from a manifest that declares all runtime
        // DLLs as private files. This bypasses SxS assembly resolution entirely.
        string crtManifest = System.IO.Path.Combine(dllDir, "checksumhelper.sxs.manifest");
        IntPtr actCookie = IntPtr.Zero;
        if (System.IO.File.Exists(crtManifest))
        {
            ACTCTX ctx = new ACTCTX();
            ctx.cbSize = Marshal.SizeOf(typeof(ACTCTX));
            ctx.lpSource = crtManifest;
            ctx.lpAssemblyDirectory = dllDir;
            ctx.dwFlags = ACTCTX_FLAG_ASSEMBLY_DIRECTORY_VALID;
            IntPtr hCtx = CreateActCtx(ref ctx);
            if (hCtx != (IntPtr)(-1))
                ActivateActCtx(hCtx, out actCookie);
        }

        // Load DLL
        IntPtr hDll = LoadLibraryEx(System.IO.Path.GetFullPath(dllPath), IntPtr.Zero, 0x00000008);
        if (hDll == IntPtr.Zero)
            hDll = LoadLibrary(System.IO.Path.GetFullPath(dllPath));
        if (hDll == IntPtr.Zero)
        {
            int err = Marshal.GetLastWin32Error();
            Console.WriteLine("{\"error\":\"LoadLibrary failed (win32=" + err + "). Install VC++ 2005 SP1 x86.\",\"code\":2,\"win32\":" + err + "}");
            return 2;
        }

        // Find DefDLLProc export
        IntPtr fnPtr = GetProcAddress(hDll, "_DefDLLProc@12");
        if (fnPtr == IntPtr.Zero)
            fnPtr = GetProcAddress(hDll, "DefDLLProc");
        if (fnPtr == IntPtr.Zero)
        {
            Console.WriteLine("{\"error\":\"DefDLLProc not found\",\"code\":2}");
            FreeLibrary(hDll);
            return 2;
        }

        var fn = (DefDLLProcDelegate)Marshal.GetDelegateForFunctionPointer(fnPtr, typeof(DefDLLProcDelegate));

        // Read ROM
        byte[] rom;
        try { rom = File.ReadAllBytes(inputFile); }
        catch (Exception ex)
        {
            Console.WriteLine("{\"error\":\"cannot read input: " + ex.Message.Replace("\"", "'") + "\",\"code\":3}");
            FreeLibrary(hDll);
            return 3;
        }

        // Pin ROM buffer and call DLL
        GCHandle pin = GCHandle.Alloc(rom, GCHandleType.Pinned);
        int result;
        try
        {
            result = fn(pin.AddrOfPinnedObject(), (uint)rom.Length, opcode);
        }
        finally
        {
            pin.Free();
        }

        if (opcode == 103)
        {
            // Correct mode: always write the modified ROM regardless of DLL return code.
            // Many DLLs return non-zero on success (e.g. number of corrections applied).
            try { File.WriteAllBytes(outputFile, rom); }
            catch
            {
                Console.WriteLine("{\"error\":\"cannot write output\",\"code\":3,\"dll_result\":" + result + "}");
                FreeLibrary(hDll);
                return 3;
            }
            Console.WriteLine("{\"ok\":true,\"opcode\":" + opcode + ",\"dll_result\":" + result + ",\"rom_size\":" + rom.Length + "}");
            FreeLibrary(hDll);
            return 0; // always return success — the ROM is written
        }
        else
        {
            // Verify mode
            Console.WriteLine("{\"ok\":" + (result == 0 ? "true" : "false") + ",\"opcode\":" + opcode + ",\"dll_result\":" + result + ",\"rom_size\":" + rom.Length + "}");
            FreeLibrary(hDll);
            return (result == 0) ? 0 : 1;
        }
    }
}
