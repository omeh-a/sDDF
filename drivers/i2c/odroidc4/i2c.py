# i2c.py
# Temporary build script for i2c. Takes in client configuration and outputs a .h
# file for the i2c driver to use.
# Matt Rossouw (matthew.rossouw@unsw.edu.au)
# 09/2023


import os
import sys

# Get the current directory
current_dir = os.path.dirname(os.path.realpath(__file__))

# Get args: each should be a comma separated list of parameters for each i2c interface, e.g.
#           i2c.py m2=0x80808080,0x80808081 m3=0x80808082,0x80808083

for interface in sys.argv:
    if interface == sys.argv[0]:
        continue
    interface_name = interface.split('=')[0]
    interface_params = interface.split('=')[1].split(',')
    interface_params = [int(x, 16) for x in interface_params]
    if len(interface_params) != 2:
        print("Error: Incorrect number of parameters for interface " + interface_name)
        exit(1)
    # Open the output file
    output_file = open(current_dir + "/i2c_" + interface_name + ".h", "w")
    # Write the header
    output_file.write("#ifndef I2C_" + interface_name.upper() + "_H\n")
    output_file.write("#define I2C_" + interface_name.upper() + "_H\n\n")
    # Write the parameters
    output_file.write("#define I2C_" + interface_name.upper() + "_BASE " + hex(interface_params[0]) + "\n")
    output_file.write("#define I2C_" + interface_name.upper() + "_IRQ " + hex(interface_params[1]) + "\n\n")
    # Write the footer
    output_file.write("#endif\n")
    output_file.close()