// Stub implementations for lgpio functions on x86_64
// These allow the code to compile but GPIO operations will be no-ops

#include <stddef.h>

// Use __attribute__((unused)) to suppress unused parameter warnings
int lgGpiochipOpen(int gpiochip __attribute__((unused))) { 
    return 0; 
}

int lgGpiochipClose(int handle __attribute__((unused))) { 
    return 0; 
}

int lgGpioClaimOutput(int handle __attribute__((unused)), 
                      int flags __attribute__((unused)), 
                      int gpio __attribute__((unused)), 
                      int level __attribute__((unused))) { 
    return 0; 
}

int lgGpioWrite(int handle __attribute__((unused)), 
                int gpio __attribute__((unused)), 
                int level __attribute__((unused))) { 
    return 0; 
}

int lgSpiOpen(int spiDev __attribute__((unused)), 
              int spiChan __attribute__((unused)), 
              int spiBaud __attribute__((unused)), 
              int spiFlags __attribute__((unused))) { 
    return 0; 
}

int lgSpiClose(int handle __attribute__((unused))) { 
    return 0; 
}

int lgSpiWrite(int handle __attribute__((unused)), 
               const char *txBuf __attribute__((unused)), 
               int count) { 
    return count; 
}