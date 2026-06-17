import sys

def init_windows_ansi():
    if sys.platform != "win32":
        return
    try:
        import ctypes
        kernel32 = ctypes.windll.kernel32
        STD_OUTPUT_HANDLE = -11
        ENABLE_VIRTUAL_TERMINAL_PROCESSING = 0x0004
        handle = kernel32.GetStdHandle(STD_OUTPUT_HANDLE)
        mode = ctypes.c_uint32()
        if kernel32.GetConsoleMode(handle, ctypes.byref(mode)):
            kernel32.SetConsoleMode(handle, mode.value | ENABLE_VIRTUAL_TERMINAL_PROCESSING)
    except:
        pass

def show_256_colors():
    print("256 kolorów terminala (16x16):\n")
    for i in range(16):
        for j in range(16):
            code = i * 16 + j
            print(f"\033[48;5;{code}m {code:>3} \033[0m", end=" ")
        print()
    print("\n\n16 kolorów podstawowych:")
    for i in range(8):
        print(f"\033[{22 + i}m {30 + i:>3} \033[0m", end=" ")
    print()
    for i in range(8):
        print(f"\033[{22 + i};1m {90 + i:>3} \033[0m", end=" ")
    print()

if __name__ == "__main__":
    init_windows_ansi()
    show_256_colors()
