/******************************************************************************
* File Name: main.c
*
* Version: 1.10
*
* Description: This is the source code for a VFD Clock.
*
* Reference: https://github.com/Kokovec/VFD_Clock/tree/main
*
*******************************************************************************
* GNU General Public License v3.0
*******************************************************************************
*
* Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND, 
* EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED 
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress 
* reserves the right to make changes to the Software without notice. The author 
* does not assume any liability arising out of the application or use of the 
* Software or any product or circuit described in the Software. Cypress does 
* not authorize its products for use in any products where a malfunction or 
* failure of the Cypress product may reasonably be expected to result in 
* significant property damage, injury or death (“High Risk Product”). By 
* including Cypress’s product in a High Risk Product, the manufacturer of such 
* system or application assumes all risk of such use and in doing so agrees to 
* indemnify Cypress against all liability.
*******************************************************************************

* This is code for a VFD clock.
* All source code was created using GROK.
* Not one line of code was written by a human
*
* The clock display will go blank after the PIR sensor has not detected movement for 5 minuts.
* Therea are 6 brightness settings that can be controlled by a butt.
* The clock can be set using UP and DOWN buttons.
* Clock uses a battery backed Real Time Clock (RTC) board.
* The circuit uses an H-Bridge chip to drive the filaments, and VFD driver chips to drive the display.
* The VFD filaments are driven at ~2VAC (pot adjustable)
* The VFD grids and segments are driven at 24VDC by using a boost converter.

*/



#include <project.h>

// Function prototypes
void UpdateDisplayTime(void);
void DisplayMultiplexed(uint8_t position);
void MultiplexDisplay(void);
void ReadTimeFromDS1307(void);
void WriteTimeToDS1307(void);
void InitializeDS1307(uint8_t *initialized);
void CheckPIRSensor(void);

// Segment patterns for digits 0-9 (A, B, C, D, E, F, G), not inverted
static const uint8_t segment_patterns[10] = {
    0x3F, // 0
    0x06, // 1
    0x5B, // 2
    0x4F, // 3
    0x66, // 4
    0x6D, // 5
    0x7D, // 6
    0x07, // 7
    0x7F, // 8
    0x6F  // 9
};

// DS1307 I2C address
#define DS1307_ADDRESS 0x68 // 7-bit address (1101000)

// Array to store the four digits to display (0-9)
static volatile uint8_t display_digits[4] = {0, 0, 0, 0};
static volatile uint8_t current_digit = 0;

// Time variables (read from DS1307)
static volatile uint8_t hours = 0;   // 0-23 (internal 24-hour format)
static volatile uint8_t minutes = 0; // 0-59
static volatile uint8_t seconds = 0; // 0-59
static volatile uint8_t dots_on = 1; // 1 = dots on, 0 = dots off

// Brightness control variables
#define BRIGHTNESS_LEVELS 6
static const uint8_t brightness_values[BRIGHTNESS_LEVELS] = {
    26,  // Level 0: 10% brightness
    51,  // Level 1: 20% brightness
    102, // Level 2: 40% brightness
    153, // Level 3: 60% brightness
    204, // Level 4: 80% brightness
    255  // Level 5: 100% brightness
};
static volatile uint8_t brightness_level = 0; // Start at lowest brightness (Level 0)
static volatile uint16_t current_brightness = 26; // Current PWM duty cycle (start at level 0)
static volatile uint16_t target_brightness = 26; // Target PWM duty cycle
static volatile uint16_t start_brightness = 26; // Starting brightness for the fade
static volatile uint8_t fading = 0; // 1 if fading is in progress, 0 otherwise
static volatile uint32_t fade_start_time = 0; // Start time of the fade
#define FADE_DURATION_MS 200 // Fade duration in milliseconds
#define FADE_STEP_MS 10 // Update brightness every 10ms

// Display control variables for PIR detection
static volatile uint8_t display_on = 0; // 0 = display off, 1 = display on
static volatile uint32_t display_timeout = 0; // Timestamp when display should turn off
#define DISPLAY_TIMEOUT_MS (3 * 60 * 1000) // 5 minutes in milliseconds (changed from 10 seconds)

// Time-setting mode variables
static volatile uint8_t time_setting_mode = 0; // 0 = normal, 1 = setting time
static volatile uint32_t button_press_start = 0; // Timestamp of button press (in ms)
static volatile uint8_t button_pressed = 0; // 0 = none, 1 = UP, 2 = DOWN
static volatile uint8_t adjustment_speed = 0; // 0 = single, 1 = moderate, 2 = fast
static volatile uint32_t last_button_activity = 0; // Timestamp of last button activity (in ms)

// Millisecond counter for timing
static volatile uint32_t tick_count = 0;

// I2C error flag
static volatile uint8_t i2c_error = 0;

/**
 * Check PIR sensor and manage display state.
 */
void CheckPIRSensor(void) {
    if (Pin_PIR_Read() == 1) { // Motion detected
        if (!display_on) {
            PWM_2_WriteCompare(current_brightness);
            display_on = 1;
        }
        display_timeout = tick_count + DISPLAY_TIMEOUT_MS; // Reset the timeout
    } else if (display_on && (tick_count >= display_timeout)) {
        // Turn off the display if timeout is reached
        PWM_2_WriteCompare(0);
        display_on = 0;
    }
}

/**
 * ISR handler for Timer_3 (1ms ticks).
 */
CY_ISR(TickInterruptHandler) {
    Timer_3_ReadStatusRegister();
    tick_count++; // Increment millisecond counter
}

/**
 * ISR handler for button press (negative edge from Debouncer).
 */
static volatile uint32_t last_brightness_interrupt_time = 0;
CY_ISR(ButtonPressInterruptHandler) {
    isr_3_ClearPending(); // Clear the interrupt
    if (tick_count - last_brightness_interrupt_time < 50) return; // Software debounce
    last_brightness_interrupt_time = tick_count;
    
    // Only start a new fade if not currently fading
    if (!fading) {
        // Store the current brightness as the starting point for the fade
        start_brightness = current_brightness;
        
        // Increase brightness level, loop back to min if at maximum
        if (brightness_level >= BRIGHTNESS_LEVELS - 1) {
            brightness_level = 0; // Loop back to min brightness
        } else {
            brightness_level++;
        }
        
        // Set the target brightness and start fading
        target_brightness = brightness_values[brightness_level];
        fading = 1;
        fade_start_time = tick_count;
        
        // If display is off, turn it on at the new brightness
        if (!display_on) {
            PWM_2_WriteCompare(current_brightness);
            display_on = 1;
            display_timeout = tick_count + DISPLAY_TIMEOUT_MS;
        }
    }
}

/**
 * ISR handler for UP button press (negative edge).
 */
static volatile uint32_t last_up_interrupt_time = 0;
CY_ISR(UpButtonPressInterruptHandler) {
    isr_4_ClearPending(); // Clear the interrupt
    if (tick_count - last_up_interrupt_time < 50) return; // Software debounce
    last_up_interrupt_time = tick_count;
    
    if (!time_setting_mode) {
        time_setting_mode = 1;
        seconds = 0;
        dots_on = 1;
        adjustment_speed = 0; // Reset speed only when entering time-setting mode
    }
    
    if (button_pressed == 0) {
        button_pressed = 1; // UP button
        button_press_start = tick_count;
        last_button_activity = tick_count;
        
        // Immediate single-minute adjustment
        minutes++;
        if (minutes >= 60) {
            minutes = 0;
            hours++;
            if (hours >= 24) {
                hours = 0;
            }
        }
        WriteTimeToDS1307();
        UpdateDisplayTime();
    }
}

/**
 * ISR handler for DOWN button press (negative edge).
 */
static volatile uint32_t last_down_interrupt_time = 0;
CY_ISR(DownButtonPressInterruptHandler) {
    isr_5_ClearPending(); // Clear the interrupt
    if (tick_count - last_down_interrupt_time < 50) return; // Software debounce
    last_down_interrupt_time = tick_count;
    
    if (!time_setting_mode) {
        time_setting_mode = 1;
        seconds = 0;
        dots_on = 1;
        adjustment_speed = 0; // Reset speed only when entering time-setting mode
    }
    
    if (button_pressed == 0) {
        button_pressed = 2; // DOWN button
        button_press_start = tick_count;
        last_button_activity = tick_count;
        
        // Immediate single-minute adjustment
        if (minutes == 0) {
            minutes = 59;
            if (hours == 0) {
                hours = 23;
            } else {
                hours--;
            }
        } else {
            minutes--;
        }
        WriteTimeToDS1307();
        UpdateDisplayTime();
    }
}

/**
 * Check if DS1307 needs initialization and set time to 11:11 AM if so.
 */
void InitializeDS1307(uint8_t *initialized) {
    uint8_t buffer;
    uint8_t status;
    uint8_t retries = 3;
    
    *initialized = 0;
    
    while (retries > 0) {
        status = I2C_1_MasterSendStart(DS1307_ADDRESS, I2C_1_WRITE_XFER_MODE);
        if (status != I2C_1_MSTR_NO_ERROR) {
            i2c_error = 1;
            I2C_1_MasterSendStop();
            retries--;
            CyDelay(10);
            continue;
        }
        status = I2C_1_MasterWriteByte(0x00);
        if (status != I2C_1_MSTR_NO_ERROR) {
            i2c_error = 1;
            I2C_1_MasterSendStop();
            retries--;
            CyDelay(10);
            continue;
        }
        I2C_1_MasterSendStop();
        
        status = I2C_1_MasterSendStart(DS1307_ADDRESS, I2C_1_READ_XFER_MODE);
        if (status != I2C_1_MSTR_NO_ERROR) {
            i2c_error = 1;
            I2C_1_MasterSendStop();
            retries--;
            CyDelay(10);
            continue;
        }
        buffer = I2C_1_MasterReadByte(I2C_1_NAK_DATA);
        I2C_1_MasterSendStop();
        
        if (I2C_1_MasterStatus() & (I2C_1_MSTAT_ERR_ADDR_NAK | I2C_1_MSTAT_ERR_XFER)) {
            i2c_error = 1;
            retries--;
            CyDelay(10);
            continue;
        }
        
        i2c_error = 0;
        break;
    }
    
    if (i2c_error) {
        return;
    }
    
    if (buffer & 0x80) {
        hours = 11;
        minutes = 11;
        seconds = 0;
        WriteTimeToDS1307();
        
        ReadTimeFromDS1307();
        if (i2c_error || hours != 11 || minutes != 11 || seconds != 0) {
            i2c_error = 1;
        } else {
            *initialized = 1;
        }
    }
}

/**
 * Read time from DS1307.
 */
void ReadTimeFromDS1307(void) {
    uint8_t buffer[3];
    uint8_t status;
    
    status = I2C_1_MasterSendStart(DS1307_ADDRESS, I2C_1_WRITE_XFER_MODE);
    if (status != I2C_1_MSTR_NO_ERROR) {
        i2c_error = 1;
        I2C_1_MasterSendStop();
        return;
    }
    status = I2C_1_MasterWriteByte(0x00);
    if (status != I2C_1_MSTR_NO_ERROR) {
        i2c_error = 1;
        I2C_1_MasterSendStop();
        return;
    }
    I2C_1_MasterSendStop();
    
    status = I2C_1_MasterSendStart(DS1307_ADDRESS, I2C_1_READ_XFER_MODE);
    if (status != I2C_1_MSTR_NO_ERROR) {
        i2c_error = 1;
        I2C_1_MasterSendStop();
        return;
    }
    buffer[0] = I2C_1_MasterReadByte(I2C_1_ACK_DATA);
    buffer[1] = I2C_1_MasterReadByte(I2C_1_ACK_DATA);
    buffer[2] = I2C_1_MasterReadByte(I2C_1_NAK_DATA);
    I2C_1_MasterSendStop();
    
    if (I2C_1_MasterStatus() & (I2C_1_MSTAT_ERR_ADDR_NAK | I2C_1_MSTAT_ERR_XFER)) {
        i2c_error = 1;
        return;
    }
    
    seconds = (buffer[0] & 0x0F) + ((buffer[0] >> 4) * 10);
    minutes = (buffer[1] & 0x0F) + ((buffer[1] >> 4) * 10);
    
    if (buffer[2] & 0x40) {
        uint8_t hour_bcd = buffer[2] & 0x1F;
        hours = (hour_bcd & 0x0F) + ((hour_bcd >> 4) * 10);
        if (buffer[2] & 0x20) {
            hours = (hours % 12) + 12;
        } else {
            hours = hours % 12;
        }
    } else {
        hours = (buffer[2] & 0x3F) + ((buffer[2] >> 4) * 10);
    }
    
    dots_on = (seconds % 2 == 0) ? 1 : 0;
}

/**
 * Write time to DS1307.
 */
void WriteTimeToDS1307(void) {
    uint8_t buffer[3];
    uint8_t status;
    
    buffer[0] = ((seconds / 10) << 4) | (seconds % 10);
    buffer[1] = ((minutes / 10) << 4) | (minutes % 10);
    
    uint8_t display_hours = hours;
    uint8_t am_pm = 0;
    if (hours == 0) {
        display_hours = 12;
    } else if (hours == 12) {
        display_hours = 12;
        am_pm = 1;
    } else if (hours > 12) {
        display_hours = hours - 12;
        am_pm = 1;
    }
    buffer[2] = 0x40;
    buffer[2] |= (am_pm << 5);
    buffer[2] |= ((display_hours / 10) << 4) | (display_hours % 10);
    
    status = I2C_1_MasterSendStart(DS1307_ADDRESS, I2C_1_WRITE_XFER_MODE);
    if (status != I2C_1_MSTR_NO_ERROR) {
        i2c_error = 1;
        I2C_1_MasterSendStop();
        return;
    }
    status = I2C_1_MasterWriteByte(0x00);
    if (status != I2C_1_MSTR_NO_ERROR) {
        i2c_error = 1;
        I2C_1_MasterSendStop();
        return;
    }
    status = I2C_1_MasterWriteByte(buffer[0]);
    if (status != I2C_1_MSTR_NO_ERROR) {
        i2c_error = 1;
        I2C_1_MasterSendStop();
        return;
    }
    status = I2C_1_MasterWriteByte(buffer[1]);
    if (status != I2C_1_MSTR_NO_ERROR) {
        i2c_error = 1;
        I2C_1_MasterSendStop();
        return;
    }
    status = I2C_1_MasterWriteByte(buffer[2]);
    if (status != I2C_1_MSTR_NO_ERROR) {
        i2c_error = 1;
        I2C_1_MasterSendStop();
        return;
    }
    I2C_1_MasterSendStop();
    
    if (I2C_1_MasterStatus() & (I2C_1_MSTAT_ERR_ADDR_NAK | I2C_1_MSTAT_ERR_XFER)) {
        i2c_error = 1;
    }
}

/**
 * Updates the display digits based on the current time (12-hour format).
 */
void UpdateDisplayTime(void) {
    uint8_t display_hours = hours;
    
    if (display_hours == 0) {
        display_hours = 12;
    } else if (display_hours > 12) {
        display_hours -= 12;
    }
    
    uint8_t hours_tens = display_hours / 10;
    uint8_t hours_ones = display_hours % 10;
    uint8_t minutes_tens = minutes / 10;
    uint8_t minutes_ones = minutes % 10;
    
    uint8_t interrupts = CyEnterCriticalSection();
    display_digits[0] = hours_tens;
    display_digits[1] = hours_ones;
    display_digits[2] = minutes_tens;
    display_digits[3] = minutes_ones;
    CyExitCriticalSection(interrupts);
}

/**
 * Displays the current digit or dots in the multiplexing cycle.
 */
void DisplayMultiplexed(uint8_t position) {
    uint16_t data1 = 0;
    uint16_t data2 = 0x001;
    
    if (position < 4) {
        uint8_t segments = segment_patterns[display_digits[position]];
        data1 = segments & 0x7F;
        uint16_t digit_bits = 0;
        switch (position) {
            case 0: digit_bits = (1 << 0); break;
            case 1: digit_bits = (1 << 1); break;
            case 2: digit_bits = (1 << 2); break;
            case 3: digit_bits = (1 << 3); break;
        }
        data1 |= (digit_bits << 7);
    } else {
        data2 = dots_on ? 0x007 : 0x000;
    }
    
    SPIM_1_ClearTxBuffer();
    SPIM_1_ClearRxBuffer();
    
    Pin_LOAD_Write(0);
    
    SPIM_1_WriteTxData(data2);
    SPIM_1_WriteTxData(data1);
    
    while (!(SPIM_1_ReadStatus() & SPIM_1_STS_SPI_DONE));
    
    Pin_LOAD_Write(1);
    CyDelayUs(5);
    Pin_LOAD_Write(0);
}

/**
 * Multiplexes the four digits and the dots.
 */
void MultiplexDisplay(void) {
    DisplayMultiplexed(current_digit);
    current_digit = (current_digit + 1) % 5;
}

/**
 * Timer_1 interrupt handler for multiplexing.
 */
CY_ISR(MultplexInterruptHandler) {
    Timer_1_ReadStatusRegister();
    MultiplexDisplay();
}

/**
 * Main function.
 */
int main(void) {
    CyGlobalIntEnable;
    
    SPIM_1_Start();
    PWM_1_Start();
    PWM_2_Start();
    Timer_1_Start();
    Timer_3_Start();
    I2C_1_Start();
    
    // Initially turn off the display
    PWM_2_WriteCompare(0);
    display_on = 0;
    
    CyDelay(10);
    
    isr_1_StartEx(MultplexInterruptHandler);
    isr_3_StartEx(ButtonPressInterruptHandler);
    isr_4_StartEx(UpButtonPressInterruptHandler);
    isr_5_StartEx(DownButtonPressInterruptHandler);
    isr_6_StartEx(TickInterruptHandler);
    
    uint8_t initialized = 0;
    i2c_error = 0;
    InitializeDS1307(&initialized);
    if (i2c_error) {
        hours = 12;
        minutes = 12;
        seconds = 0;
        i2c_error = 0;
    }
    
    if (!initialized) {
        ReadTimeFromDS1307();
        if (i2c_error) {
            hours = 12;
            minutes = 12;
            seconds = 0;
            i2c_error = 0;
        }
    }
    UpdateDisplayTime();
    
    uint32_t last_button_check = 0;
    uint32_t last_time_read = 0;
    uint32_t last_fade_update = 0;
    uint32_t last_pir_check = 0;
    
    for (;;) {
        // Handle brightness fading
        if (fading) {
            uint32_t current_time = tick_count;
            uint32_t elapsed_time = current_time - fade_start_time;
            
            // Check if fade is complete
            if (elapsed_time >= FADE_DURATION_MS) {
                current_brightness = target_brightness;
                PWM_2_WriteCompare(display_on ? current_brightness : 0);
                fading = 0; // Stop fading
            }
            // Update brightness incrementally
            else if (current_time - last_fade_update >= FADE_STEP_MS) {
                // Calculate the progress (0 to 1) through the fade
                float progress = (float)elapsed_time / FADE_DURATION_MS;
                int16_t brightness_diff = target_brightness - start_brightness; // Difference from start to target
                current_brightness = start_brightness + (int16_t)(brightness_diff * progress);
                
                // Ensure current_brightness stays within bounds
                if (target_brightness > start_brightness) {
                    if (current_brightness > target_brightness) current_brightness = target_brightness;
                    if (current_brightness < start_brightness) current_brightness = start_brightness;
                } else {
                    if (current_brightness < target_brightness) current_brightness = target_brightness;
                    if (current_brightness > start_brightness) current_brightness = start_brightness;
                }
                
                PWM_2_WriteCompare(display_on ? current_brightness : 0);
                last_fade_update = current_time;
            }
        }
        
        // Check PIR sensor every 100ms
        if (tick_count - last_pir_check >= 100) {
            CheckPIRSensor();
            last_pir_check = tick_count;
        }
        
        // Read time from DS1307 every 500ms to update display
        if (tick_count - last_time_read >= 500) {
            if (!time_setting_mode) {
                ReadTimeFromDS1307();
                if (!i2c_error) {
                    UpdateDisplayTime();
                }
            }
            last_time_read = tick_count;
        }
        
        // Handle long press adjustments for time setting
        if (time_setting_mode && button_pressed != 0) {
            uint32_t current_time = tick_count;
            uint32_t press_duration = current_time - button_press_start;
            
            if (press_duration >= 2000) {
                adjustment_speed = 2;
            } else if (press_duration >= 500) {
                adjustment_speed = 1;
            } else {
                adjustment_speed = 0;
            }
            
            if (current_time - last_button_check >= 250) {
                uint8_t adjustment_amount = 0;
                if (adjustment_speed == 1) {
                    adjustment_amount = 1;
                } else if (adjustment_speed == 2) {
                    adjustment_amount = 5;
                }
                
                if (adjustment_amount > 0) {
                    if (button_pressed == 1) {
                        minutes += adjustment_amount;
                        while (minutes >= 60) {
                            minutes -= 60;
                            hours++;
                            if (hours >= 24) {
                                hours = 0;
                            }
                        }
                    } else if (button_pressed == 2) {
                        while (adjustment_amount > 0) {
                            if (minutes == 0) {
                                minutes = 59;
                                if (hours == 0) {
                                    hours = 23;
                                } else {
                                    hours--;
                                }
                            } else {
                                minutes--;
                            }
                            adjustment_amount--;
                        }
                    }
                    WriteTimeToDS1307();
                    if (!i2c_error) {
                        UpdateDisplayTime();
                    }
                    last_button_activity = current_time;
                }
                last_button_check = current_time;
            }
        }
        
        // Check for timeout to exit time-setting mode (5 seconds of inactivity)
        if (time_setting_mode && button_pressed == 0) {
            if (tick_count - last_button_activity >= 5000) {
                time_setting_mode = 0;
                dots_on = 1;
            }
        }
        
        // Check for button release
        if (button_pressed != 0) {
            if (button_pressed == 1 && Pin_Up_Read() == 1) {
                button_pressed = 0;
                adjustment_speed = 0;
                last_button_activity = tick_count;
            } else if (button_pressed == 2 && Pin_Down_Read() == 1) {
                button_pressed = 0;
                adjustment_speed = 0;
                last_button_activity = tick_count;
            }
        }
    }
}
