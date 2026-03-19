#!/usr/bin/env python3
"""
SPDX-License-Identifier: GPL-2.0-only */
Copyright (C) 2022-2025, Verdant Consultants, LLC.

PiTrac Controller Board Strobe Output Calibration Module

Adjusts the strobe output to a default (or selected) current output by sending a range of values to the digital 
potentiometer on the board (via SPI) and then repeatedly checking the ADC to see if the desired output has been
reached (as the LED current iteratively goes down as up DAC output goes down).

WARNING ------  This code has not been tested against actual hardware yet, so it could still be harmful
                to whatever hardware you are running on.  Caveat emptor!
                Please run this step by step in a debugger to make sure that it is sending appropriate
                data to the Controller Board.

"""
import spidev
# Will need to run 
# sudo apt install python3-rpi.gpio
# sudo apt install python3-lgpio
from gpiozero import LED
import time
import sys
import argparse
import os
import gc


import logging

# We expect to be running this utility in the standard directory where PiTrac is installed, and where there
# should be the config.manager.py and configurations.json files.  If running somewhere else, the user will need to either put 
# a copy of those files there, or else set the PiTrac home directory appropriately.
# That directory is typically at: /usr/lib/pitrac/web-server
DEFAULT_PITRAC_PATH = "/usr/lib/pitrac/web-server"
sys.path.append(DEFAULT_PITRAC_PATH)

from config_manager import ConfigurationManager


logger = logging.getLogger(__name__)


class StrobeOutputCalibrator:

    # The final DAC setting and corresponding ADC output will be saved to the user_settings.json file, 
    # so we can read them later if needed.  These are the search keys for those values in the JSON file.
    DAC_SETTING_JSON_PATH = "gs_config.strobing.kDAC_setting"

    # These are the more-or-less standard SPI bus and device numbers for the Raspberry Pi. 
    SPI_BUS = 0
    SPI_DAC_DEVICE = 0 # DAC is on CS0
    SPI_ADC_DEVICE = 1 # ADC is on CS1 

    # Max speed for the ADC is 1.1 MHz and DAC is 20 MHz
    SPI_MAX_SPEED_HZ = 1000000  # 1 MHz

    # This is the pin that we will use to toggle the strobe output on and off through the DIAG pin 
    # on the Connector Board.  The strobe needs to be on for the ADC to read the LED current, 
    # so we will toggle this pin on just before reading the ADC, and then toggle it off just after.  
    # Note - this corresponds to the BCM pin number, not the physical pin number.  
    # So this is physical pin 38 on the Raspberry Pi header.
    DIAG_GPIO_PIN = 20

    # This is the maximum safe strobe current for the V3 LED
    V3_TARGET_LED_CURRENT_SETTING = 10.0 # amps
    # This is the maximum safe strobe current for the old 100W LED
    OLD_TARGET_LED_CURRENT_SETTING = 7.0 # amps


    # We should NEVER go below this LDO voltage
    ABSOLUTE_LOWEST_LDO_VOLTAGE = 4.5 # volts
    ABSOLUTE_HIGHEST_LDO_VOLTAGE = 11 # volts

    MAX_DAC_SETTING = 0xFF # 255  # It's an 8-bit DAC, so max value is 2^8 - 1
    MIN_DAC_SETTING = 0 # Note - 

    # This value is just a guess for now and works on 1 board.  It should be high enough so that the LDO voltage in all devices
    # (even accounting for variations) will be low enough not to hurt our standard strobe, but high enough to be above the minimum LDO voltage.
    # This settins will only be used to set the DAC to a known, safe level if there's some failure
    # in the calibration process.
    PRESUMED_SAFE_DAC_SETTING = 0x96 # 150

    # For the MCP4801 commands, see: https://ww1.microchip.com/downloads/en/DeviceDoc/22244B.pdf
    MCP4801_BASE_WRITE_CMD_SET_OUTPUT = 0b00110000  # Standard (1x) gain (bit 13) (Vout=Vref*D/4096), VREF buffered, active, no shutdown, D4-D7 are 0 

    # For the MCP3202 commands, see: https://www.google.com/aclk?sa=L&ai=DChsSEwj3ncmJqIKTAxWQK60GHXKrGJQYACICCAEQABoCcHY&ae=2&co=1&ase=2&gclid=CjwKCAiAh5XNBhAAEiwA_Bu8Fae9UWeNq0dHjoQ9N4wHFrRnEvETyzrYVl5xmGvUyNR0uFNVP5IlQRoCaAcQAvD_BwE&cid=CAASWeRo2UUWEQuONJENmHLbquopI4y29-vAFb0frx7hQM6bjuRo_TZk1PIihnFuJO8jpNwTGiEFOnOQWgmJHHFTivCGeaaKvgUk8x67yLoRpN_bIoGgiweiu6Vk&cce=2&category=acrcp_v1_71&sig=AOD64_3LJYqvTzD3ozkW0pkVDNgovi6vvA&q&nis=4&adurl&ved=2ahUKEwi1w8KJqIKTAxXKFDQIHVsXEX8Q0Qx6BAgPEAE
    MCP3202_READ_CH0_SINGLE_ENDED_CMD = 0 | 0x80  # Channel 0, single-ended - LED Current Sense Resistor Voltage
    MCP3202_READ_CH1_SINGLE_ENDED_CMD = 0 | 0xc0  # Channel 1, single-ended - LDO Gate Voltage

    # Will be set when the class is initialized
    spi_dac = None
    spi_adc = None

    diag_pin = None

    def __init__(self):

        self.config_manager = ConfigurationManager()


    def setup_spi_channels(self):
        logger.debug(f"Setting up SPI channels...")

        success = False

        try:
            # DAC Setup
            self.spi_dac = spidev.SpiDev()
            self.spi_dac.open(self.SPI_BUS, self.SPI_DAC_DEVICE)

            # Set SPI speed
            self.spi_dac.max_speed_hz = self.SPI_MAX_SPEED_HZ

            # Set SPI mode common modes are 0 or 3)
            self.spi_dac.mode = 0b00 # Mode 0


            # ADC Setup
            self.spi_adc = spidev.SpiDev()
            self.spi_adc.open(self.SPI_BUS, self.SPI_ADC_DEVICE)

            # Set SPI speed
            self.spi_adc.max_speed_hz = self.SPI_MAX_SPEED_HZ

            # Set SPI mode common modes are 0 or 3)
            self.spi_adc.mode = 0b00 # Mode 0


            # Signal that all went well with the SPI setup
            success = True

        except Exception as e:
            logger.error(f"An error occurred when setting up the SPI connections: {e}")
            success = False

        return success


    def close_spi(self):
        if self.spi_dac is not None:
            self.spi_dac.close() # Always close the SPI connection when done
        if self.spi_adc is not None:
            self.spi_adc.close()

    def open_gpio_system(self):
        logger.debug(f"Setting up GPIO pin {self.DIAG_GPIO_PIN}...")

        success = False

        try:
            self.diag_pin = LED(self.DIAG_GPIO_PIN) # Use Broadcom pin-numbering scheme

            # Signal that all went well with the SPI setup
            success = True

        except Exception as e:
            logger.error(f"An error occurred when setting up the GPIO system: {e}")
            success = False

        return success

    def close_gpio_system(self):
        # Cleanup all GPIO pins to their default state
        if self.diag_pin is not None:
            self.diag_pin.close()

    def get_ADC_value_CH0(self):
        # Start bit is always the first byte, then the channel and mode bits are combined into the second byte, 
        # and the third byte is just a timing placeholder for the response, because we need the last 2 of 3 bytes of response,
        # but our command is only 2 bytes
        message_to_send = [0x01, self.MCP3202_READ_CH0_SINGLE_ENDED_CMD, 0x00]
        logger.debug(f"Message to send to ADC (to get value): {[format(b, '02x') for b in message_to_send]}")

        response_bytes = self.spi_adc.xfer2(message_to_send)

        # The result is 12-bits.  The first byte returned is just random - the MISO line is null 
        # when the command is sent, so nothing was really sent.  
        # The second byte contains the top 4 bits (masked with 0x0F as some bits may be null)
        # The third byte contains the least-significant 8 bits
    
        # Put the top 4 bits and lower 8 bits together to get the full 12-bit ADC value
        adc_value = (response_bytes[1] & 0x0F) << 8 | response_bytes[2]

        return adc_value

    def get_ADC_value_CH1(self):
        # Start bit is always the first byte, then the channel and mode bits are combined into the second byte, 
        # and the third byte is just a timing placeholder for the response, because we need the last 2 of 3 bytes of response,
        # but our command is only 2 bytes
        message_to_send = [0x01, self.MCP3202_READ_CH1_SINGLE_ENDED_CMD, 0x00]
        logger.debug(f"Message to send to ADC (to get value): {[format(b, '02x') for b in message_to_send]}")

        response_bytes = self.spi_adc.xfer2(message_to_send)

        # The result is 12-bits.  The first byte returned is just random - the MISO line is null 
        # when the command is sent, so nothing was really sent.  
        # The second byte contains the top 4 bits (masked with 0x0F as some bits may be null)
        # The third byte contains the least-significant 8 bits
    
        # Put the top 4 bits and lower 8 bits together to get the full 12-bit ADC value
        adc_value = (response_bytes[1] & 0x0F) << 8 | response_bytes[2]

        return adc_value


    def get_LDO_voltage(self):
        # We need to measure LDO voltage to make sure we are safe to raise the DIAG pin to high
        adc_value = self.get_ADC_value_CH1()

        # *3 because of the resistor divider made up of 2k top and 1k bottom, so (1 / (2 + 1)) scaling factor
        LDO_voltage = (3.3 / 4096) * adc_value * 3  # Convert ADC value to voltage
        return LDO_voltage
    
    def get_LED_current(self):
        # We need to turn on the strobe output through the DIAG pin before we read the ADC, 
        # because a valid LED current sense voltage is only present when the strobe is on.
        # and then turn it right back off again.
        message_to_send = [0x01, self.MCP3202_READ_CH0_SINGLE_ENDED_CMD, 0x00]
        logger.debug(f"Message to send to ADC (to get value): {[format(b, '02x') for b in message_to_send]}")

        spi = self.spi_adc
        diag = self.diag_pin

        # --- PREPARE FOR CRITICAL TIMING ---
        
        # Disable Python's random memory cleaning
        gc.disable() 
        
        # Grab the highest possible Real-Time OS Priority
        try:
            param = os.sched_param(os.sched_get_priority_max(os.SCHED_FIFO))
            os.sched_setscheduler(0, os.SCHED_FIFO, param)
        except (PermissionError, AttributeError):
            logger.debug(f"WARNING: sudo permissions not established or OS scheduling priority not supported.")
            pass # Fails if not running as root/sudo or on Windows
            
        # Yield CPU to get a fresh, full time-slice from Linux
        time.sleep(0) 

        
        try:
            # --- BEGIN DETERMINISTIC HARDWARE BLOCK ---
            diag.on()
            response_bytes = spi.xfer2(message_to_send)
            diag.off()
            # --- END DETERMINISTIC HARDWARE BLOCK ---

        finally:
            # --- RETURN TO NORMAL OS BEHAVIOR ---
        
            # Give up real-time priority (return to normal scheduler)
            try:
                param = os.sched_param(0)
                os.sched_setscheduler(0, os.SCHED_OTHER, param)
            except (PermissionError, AttributeError):
                pass
                
            # Turn memory management back on
            gc.enable() 
        
        adc_value = (response_bytes[1] & 0x0F) << 8 | response_bytes[2]
        LED_current = (3.3 / 4096) * adc_value * 10  # Convert ADC value to current
        return LED_current

    def turn_diag_pin_off(self):
        logger.debug(f"turn_diag_pin_off")
        if self.diag_pin is not None:
            self.diag_pin.off()


    def short_pause(self):
        time.sleep(0.1)


    def set_DAC(self, value):
        msb_data = self.MCP4801_BASE_WRITE_CMD_SET_OUTPUT | ((value >> 4) & 0x0F)  # Get the top 4 bits of the value and combine with the command
        
        lsb_data = (value << 4) & 0xF0  # Get the bottom 4 bits of the value into the top 4 bits of the second byte (the bottom 4 bits of the second byte 
                                        # are ignored by the DAC, so it doesn't matter what we put there)

        message_to_send = [msb_data, lsb_data]  # Get the pot value

        logger.debug(f"\nset_DAC:  Message to send to DAC: {[format(b, '02x') for b in message_to_send]}")
        # We don't use the response
        response = self.spi_dac.xfer2(message_to_send)


    def get_calibration_settings_from_json(self):

        current_DAC_output_value = -1
        current_DAC_setting = self.config_manager.get_config(self.DAC_SETTING_JSON_PATH)
        if current_DAC_setting is None:
            logger.debug(f"Current DAC Setting: <Not set in user_settings.json - search key was {self.DAC_SETTING_JSON_PATH}>")
        else:
            logger.debug(f"Current DAC Setting: {current_DAC_setting}")
            current_DAC_output_value = int(current_DAC_setting)

        return current_DAC_output_value

    
    def set_DAC_to_safest_level(self):
        logger.error(f"Calibration failed.  Setting DAC voltage to highest level of {self.PRESUMED_SAFE_DAC_SETTING} (presumed-safe strobe level) for safety.")    
        self.set_DAC(self.PRESUMED_SAFE_DAC_SETTING)


    def json_file_has_calibration_settings(self):
        logger.debug(f"Checking whether json_file_has_calibration_settings")
        current_DAC_setting = self.config_manager.get_config(self.DAC_SETTING_JSON_PATH)
        if current_DAC_setting is None:
            return False
        else:
            return True

    def find_DAC_start_setting(self):
        DAC_max_setting = self.MAX_DAC_SETTING + 1
        for i in range(DAC_max_setting):
                DAC_start_setting = i
                # set DAC value
                self.set_DAC(i)
                # wait for DAC value to take effect
                self.short_pause()
                # check the LDO voltage
                LDO_voltage = self.get_LDO_voltage()
                logger.debug(f"DAC Value: {format(i, '02x')}, Computed LDO voltage (from ADC): {format(LDO_voltage, '0.2f')}" )
                # if LDO voltage drops below ABSOLUTE_LOWEST_LDO_VOLTAGE then break the loop
                if LDO_voltage < self.ABSOLUTE_LOWEST_LDO_VOLTAGE:
                    # set starting DAC value to lowest voltage above the absolute minimum
                    DAC_start_setting = i - 1
                    break
                
        return DAC_start_setting, LDO_voltage


    def calibrate_board(self, target_LED_current):

            # find the minimum safe LDO voltage to supply the MCP1407 gate driver
            DAC_start_setting, LDO_voltage = self.find_DAC_start_setting()
            
            # If even a DAC value of 0 was below the ABSOLUTE_LOWEST_LDO_VOLTAGE then fail calibration
            if DAC_start_setting < 0:
                logger.debug(f"DAC value of 0 is below minimum LDO voltage ({format(self.ABSOLUTE_LOWEST_LDO_VOLTAGE, '0.2f')}): {format(LDO_voltage, '0.2f')}")
                return False, -1, -1


            logger.debug(f"calibrate_board called with target_LED_current = {target_LED_current}, DAC_start_setting = 0x{format(DAC_start_setting, '02x')}")

            # Now, starting at the max DAC value (0xFF)
            # we will iteratively decrease the DAC setting until we get to 
            # the desired ADC output (or just under it)

            current_DAC_setting = DAC_start_setting
            final_DAC_setting = self.MIN_DAC_SETTING

            # just picking a number that we should always be above at the start of the loop, 
            # so that we can save the first reading as the best one so far even if it's not 
            # above the target
            max_LED_current_so_far = 0.0  

            # We will start at the max DAC setting and then count down while 
            # looking for the point where the corresponding LED current goes just above the target_LED_current, 
            # then increase 1 value to ensure we are <= target_LED_current.  

            logger.debug(f"calibrate_board starting loop.  Desired output is {target_LED_current}")

            # Stop immediately if we ever have an error
            while (current_DAC_setting >= self.MIN_DAC_SETTING):

                self.set_DAC(current_DAC_setting)

                # Wait a moment for the setting to take effect
                self.short_pause()

                # check the LDO voltage to ensure that we are within the safe bounds
                LDO_voltage = self.get_LDO_voltage()
                # if we are below the ABSOLUTE_LOWEST_LDO_VOLTAGE, it is unsafe to pulse the DIAG pin. Decrease DAC value and continue
                if LDO_voltage < self.ABSOLUTE_LOWEST_LDO_VOLTAGE:
                    logger.debug(f"Measured LDO_voltage ({LDO_voltage}) was below ABSOLUTE_LOWEST_LDO_VOLTAGE of {self.ABSOLUTE_LOWEST_LDO_VOLTAGE} volts.  Trying next DAC value.")
                    # Continue counting down
                    final_DAC_setting = current_DAC_setting
                    current_DAC_setting -= 1
                    continue
                
                # if we are above the ABSOLUTE_HIGHEST_LDO_VOLTAGE, then we have to stop and fail the calibration
                if LDO_voltage > self.ABSOLUTE_HIGHEST_LDO_VOLTAGE:
                    logger.debug(f"Measured LDO_voltage ({LDO_voltage}) was above ABSOLUTE_HIGHEST_LDO_VOLTAGE of {self.ABSOLUTE_HIGHEST_LDO_VOLTAGE} volts.  Stopping calibration, as something is wrong.")
                    return False, -1, -1
                
                # Note reading the LED current also pulses the strobe through the DIAG pin,
                # which is necessary to get a valid reading, but also means that we are toggling 
                # the strobe on and off repeatedly during this calibration process, which is not ideal.  
                # But we need to do it in order to get accurate LED current readings.
                LED_current = self.get_LED_current()
                logger.debug(f"current_DAC_setting: {format(current_DAC_setting, '02x')}, LED_current: {LED_current}")

                # As we are slowly increasing the LED current, have we reached our desired set-point for the LED current yet?
                if LED_current > target_LED_current:
                    logger.debug(f"    ---> Reached above the target_LED_current ({target_LED_current}). LED_current is: {LED_current}.  Stopping calibration here...")
                    final_DAC_setting = current_DAC_setting + 1  # Step back to the last setting that was just before we reached our target
                    break

                # We have not yet reached the target.  TBD - This is a little redundant, maybe change
                # Keep track of where we were.
                if LED_current > max_LED_current_so_far:

                    # Save the current output as the best one so far, even if it's not over the target, 
                    # because we want to get as close as possible without going over
                    max_LED_current_so_far = LED_current

                # Continue counting down
                final_DAC_setting = current_DAC_setting
                current_DAC_setting -= 1

            # There are a couple of possible edge cases here.  And either of them indicate that something probably went wrong somewhere even if we
            # thought we had a success.
            # If so, err on the safe side and consider this a failure
            if current_DAC_setting <= self.MIN_DAC_SETTING:
                logger.debug(f"Reached MIN_DAC_SETTING ({self.MIN_DAC_SETTING}) without ever reaching target_LED_current ({target_LED_current}).  This generally indicates a problem.  Failing calibration.")
                return False, -1, -1
            if current_DAC_setting >= self.MAX_DAC_SETTING:
                logger.debug(f"The MAX_DAC_SETTING resulted in an LED current above the target.  This generally indicates a problem.  Failing calibration.")
                return False, -1, -1

            # Now, using the best DAC setting we found, average the output voltage a few times to 
            # get a more accurate reading of the output voltage at that setting
            # take an average of n pulses
            n = 10
            while True:
                self.set_DAC(final_DAC_setting)
                self.short_pause()
                
                # check if LDO voltage is above the minimum
                LDO_voltage = self.get_LDO_voltage()
                if LDO_voltage < self.ABSOLUTE_LOWEST_LDO_VOLTAGE:
                    # Fallback to the last known good measurement
                    final_DAC_setting -= 1
                    break

                # Take an average of n pulses
                LED_current_sum = 0
                for _ in range(n):
                    LED_current_sum += self.get_LED_current()
                    self.short_pause()
                LED_current = LED_current_sum / n
                
                if LED_current > target_LED_current:
                    # Current is still slightly too high, step the DAC setting
                    final_DAC_setting += 1
                else:                    
                    # We are at or below the target current, we're done
                    break

            logger.debug(f"calibrate_board -- final_DAC_setting: {format(final_DAC_setting, '02x')}, LED_current: {LED_current}")

            return True, final_DAC_setting, LED_current


    def cleanup_for_exit(self):
        self.turn_diag_pin_off()
        self.short_pause()
        self.close_spi()
        self.close_gpio_system()

    # -----------------------

    
def main():
    
    success = True

    # The calibrator class does all of the work here
    calibrator = StrobeOutputCalibrator()

    parser = argparse.ArgumentParser(description="PiTrac Controller Board Strobe Output Calibrator.  This tool iteratively adjusts the board's DAC in order to find the right setting for the desired LED current for the strobe LED circuit.\nWARNING - Setting the LDO voltage below 4.5v can break your Control Board.")
    parser.add_argument("-o", "--old_LED", action="store_true", help="PiTrac is using old 100W LED. Default behavior is V3 LED")
    parser.add_argument("-v", "--verbose", action="store_true", help="Verbose output")
    parser.add_argument("-q", "--quiet", action="store_true", help="Quiet output")
    parser.add_argument("-w", "--overwrite", action="store_true", help="Overwrites any existing strobe setting in user_settings.json")
    parser.add_argument("--target_output", default=0,type=float, help="Set target LED current output (in volts) (ADVANCED)")
    parser.add_argument("--ignore", action="store_true", help="Attempt calibration even if the Controller Board version is not 3.0 (ADVANCED)")
    action_group = parser.add_mutually_exclusive_group()
    action_group.add_argument("-p", "--print_settings", action="store_true", help="Print the current DAC setting and last ADC measurement from the user_settings.json file")
    action_group.add_argument("-a0", "--read_ADC_CH0", action="store_true", help="Measure and print the current (12-bit) ADC CH0 measurement from the Connector Board")
    action_group.add_argument("-a1", "--read_ADC_CH1", action="store_true", help="Measure and print the current (12-bit) ADC CH1 measurement from the Connector Board)")
    action_group.add_argument("-c", "--read_LED_current", action="store_true", help="Compute and print the current LED current (based on the ADC CH0 measurement) from the Connector Board")
    action_group.add_argument("-l", "--read_LDO_voltage", action="store_true", help="Compute and print the current LDO value (based on the ADC CH1 measurement) from the Connector Board")
    action_group.add_argument("--DAC_setting", default=None,type=int, help="Set the DAC input to a specific value.  Value is 8 bits long  (ADVANCED).")
    action_group.add_argument("--get_DAC_start", action="store_true", help="Check LDO voltage while sweeping DAC values to find lowest safe value")
    
    args = parser.parse_args()

    if args.verbose:
        logging.basicConfig(level=logging.DEBUG)
    elif args.quiet:
        logging.basicConfig(level=logging.WARNING)
    else:
        logging.basicConfig(level=logging.INFO)


    logger.debug(f"Calibrator initialized")

    if calibrator.setup_spi_channels() == False:
        logger.error(f"SPI initialization failed.  Cannot proceed with calibration.")
        return 1

    if calibrator.open_gpio_system() == False:
        logger.error(f"GPIO initialization failed.  Cannot proceed with calibration.")
        calibrator.close_gpio_system()
        return 1

    try:

        # Process other options

        if args.read_ADC_CH0:
            ADC_response = calibrator.get_ADC_value_CH0()
            logger.info(f"Value read from ADC: {format(ADC_response, '02x')}" )

        elif args.read_ADC_CH1:
            ADC_response = calibrator.get_ADC_value_CH1()
            logger.info(f"Value read from ADC: {format(ADC_response, '02x')}" )

        elif args.read_LDO_voltage:
            LDO_Voltage = calibrator.get_LDO_voltage()
            logger.info(f"Computed LDO voltage (from ADC): {format(LDO_Voltage, '0.2f')}" )
        
        elif args.read_LED_current:

            # ensure that the LDO voltage is within safe bounds before pulsing the DIAG pin
            LDO_Voltage = calibrator.get_LDO_voltage()
            if LDO_Voltage > calibrator.ABSOLUTE_LOWEST_LDO_VOLTAGE:
                LED_current = calibrator.get_LED_current()
                logger.info(f"Computed LED current (from ADC): {format(LED_current, '0.2f')}" )
            else:
                logger.warning(f"LDO voltage is below minimum value, cannot safely pulse DIAG pin to get LED current. LDO Voltage: {format(LDO_Voltage, '0.2f')}")
                return 1

        elif args.DAC_setting is not None:
            desired_DAC_setting = args.DAC_setting

            # Check if desired DAC setting is within the allowable bounds of MIN_DAC_SETTING and MAX_DAC_SETTING
            if desired_DAC_setting > calibrator.MAX_DAC_SETTING:
                logger.warning(f"Maximum allowable DAC setting is: {format(calibrator.MAX_DAC_SETTING, '02x')}" )
                return 1
            if desired_DAC_setting < calibrator.MIN_DAC_SETTING:
                logger.warning(f"Minimum allowable DAC setting is: {format(calibrator.MIN_DAC_SETTING, '02x')}" )
                return 1
            
            # Set the DAC value
            calibrator.set_DAC(desired_DAC_setting)
            # Wait a moment for the setting to take effect
            calibrator.short_pause()

            # check the LDO voltage
            LDO_voltage = calibrator.get_LDO_voltage()
            logger.warning(f"DAC is set to: {format(desired_DAC_setting, '02x')}" )
            if LDO_voltage < calibrator.ABSOLUTE_LOWEST_LDO_VOLTAGE:
                logger.warning(f"LDO voltage is below minimum value. This is VERY DANGEROUS. If this is unintentional, run --get_DAC_start to find the minimum safe DAC value.")

        elif args.print_settings:
            DAC_value = calibrator.get_calibration_settings_from_json()

            if DAC_value < 0:
                logger.debug(f"Current DAC Setting: <Not set in user_settings.json>")
            else:
                logger.info(f"DAC value from user settings: {format(DAC_value, '02x')}" )

        elif args.get_DAC_start:
            # sweep DAC values from low to high
            DAC_start_setting, LDO_voltage = calibrator.find_DAC_start_setting()

            # If even a DAC value of 0 was below the ABSOLUTE_LOWEST_LDO_VOLTAGE then fail calibration
            if DAC_start_setting < 0:
                logger.warning(f"DAC value of 0 is below minimum LDO voltage ({format(calibrator.ABSOLUTE_LOWEST_LDO_VOLTAGE, '0.2f')}): {format(LDO_voltage, '0.2f')}\nThis indicates a problem with the controller board")
            else:

                calibrator.set_DAC(DAC_start_setting)

                # Wait a moment for the setting to take effect
                calibrator.short_pause()

                # check the LDO voltage
                LDO_voltage = calibrator.get_LDO_voltage()

                logger.info(f"DAC_start_setting = 0x{format(DAC_start_setting, '02x')}. LDO_voltage = {format(LDO_voltage, '0.2f')}")

        else:
            # Default calibration behavior - iteratively find the closest setting for the DAC that will get the desired ADC reading (but not under)
            logger.info(f"Calibrating PiTrac Control Board.  This may take a minute or two.  Please wait..." )

            if calibrator.json_file_has_calibration_settings() and not args.overwrite:
                logger.error(f"Calibration settings already exist in user_settings.json.  Use the --overwrite flag to overwrite them.")
                return 1

            control_board_version = calibrator.config_manager.get_config("gs_config.strobing.kConnectionBoardVersion")
            logger.debug(f"control_board_version = {control_board_version}")
            if control_board_version is None:
                control_board_version = "0"

            control_board_version_value = int(control_board_version)

            # This calibration function is only relevant for the Version 3.x Control Board
            if control_board_version_value != 3 and not args.ignore:
                logger.error(f"The controller board is the wrong version ({control_board_version_value}) for this calibration utility.  Must be using a Verison 3 board.")
                return 1

            if (args.target_output > 0.0):
                target_LED_current = args.target_output
            elif (args.old_LED):
                target_LED_current = calibrator.OLD_TARGET_LED_CURRENT_SETTING
            else:
                target_LED_current = calibrator.V3_TARGET_LED_CURRENT_SETTING


            logger.debug(f"target_LED_current = {target_LED_current}")


            # Perform the actual calibration here
            success, final_DAC_setting, LED_current = calibrator.calibrate_board(target_LED_current)


            if success and final_DAC_setting > 0:
                logger.info(f"Calibration successful.  Final DAC setting: {format(final_DAC_setting, '02x')}, corresponding LED current: {format(LED_current, '0.2f')}")
                # Save the final DAC setting and corresponding ADC output to the user_settings.json file for later reference
                calibrator.config_manager.set_config(calibrator.DAC_SETTING_JSON_PATH, final_DAC_setting)
            else:
                logger.info(f"Calibration failed.")
                calibrator.set_DAC_to_safest_level()


        calibrator.short_pause()

        logger.debug(f"Calibration operation completed." )

    except KeyboardInterrupt:
        print("\nCtrl+C pressed. Performing cleanup...")
        # Add your cleanup code here (e.g., closing files, releasing resources)
        calibrator.cleanup_for_exit()

    except Exception as e:
        logger.debug(f"An error occurred: {e}")
        calibrator.set_DAC_to_safest_level()

        return 1 # Failure

    finally:
        calibrator.cleanup_for_exit()

    if success:
        return 0
    else:
        return 1


if __name__ == "__main__":
    sys.exit(main())
