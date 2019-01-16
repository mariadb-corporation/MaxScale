#!/usr/bin/env python3

# Copyright (c) 2019 MariaDB Corporation Ab
#
# Use of this software is governed by the Business Source License included
# in the LICENSE.TXT file and at www.mariadb.com/bsl11.
#
# Change Date: 2022-01-01
#
# On the date above, in accordance with the Business Source License, use
# of this software will be governed by version 2 or later of the General
# Public License.

import os
import sys
import re
import fnmatch
import zipfile
import subprocess
import datetime


def main(argv):
    maxctrl_un = ""
    maxctrl_pw = ""
    if len(argv) == 2 or len(argv) > 3:
        print("Usage: {} [<maxctr_username> <maxctrl_password]".format(argv[0]))
    elif len(argv) == 3:
        maxctrl_un = argv[1]
        maxctrl_pw = argv[2]

    main_conf_file_path = "/etc/maxscale.cnf"
    config_files_dir = "/etc/maxscale.cnf.d/"
    runtime_config_files_dir = "/var/lib/maxscale/maxscale.cnf.d/"
    log_files_dir = "/var/log/maxscale/"

    time_now = datetime.datetime.now().strftime("%y%m%d_%H%M%S")
    output_file_path = os.getcwd() + "/" + "diagnostics_files_" + time_now + ".zip"

    print("Assembling typical config and log files for submitting to support.")
    print("Using the following paths for config files: " + main_conf_file_path +
          ", " + config_files_dir + ", " + runtime_config_files_dir)
    print("Using the following paths for log files: " + log_files_dir)
    print("Output is written to " + output_file_path)
    print("\n")

    try:
        output_file = zipfile.ZipFile(output_file_path, mode='w',
                                      compression=zipfile.ZIP_DEFLATED)
    except IOError as e:
        print("Error when opening file " + output_file_path + ": " + e.strerror)
        return

    format_str = "Adding file {} to archive."
    # Write contents of primary config file
    contents = get_config_file(main_conf_file_path)
    if len(contents) > 0:
        print(format_str.format(main_conf_file_path))
        output_file.writestr(main_conf_file_path, contents)

    # Write contents of additional config files
    add_config_files_from_dir(config_files_dir, output_file)

    # Write contents of runtime config files
    add_config_files_from_dir(runtime_config_files_dir, output_file)

    # Write contents of log files
    if os.path.isdir(log_files_dir):
        for file in os.listdir(log_files_dir):
            if fnmatch.fnmatch(file, "*.log"):
                file_path = log_files_dir + file
                contents = read_log_file(file_path)
                if len(contents) > 0:
                    print(format_str.format(file_path))
                    output_file.writestr(file_path, contents)

    # Run maxctrl and add output
    contents = run_max_ctrl(maxctrl_un, maxctrl_pw)
    if len(contents) > 0:
        file_name = "maxctrl_output.txt"
        print(format_str.format(file_name))
        output_file.writestr(file_name, contents)

    # Run gdb and add output
    contents = read_core_file()
    if len(contents) > 0:
        file_name = "gdb_output.txt"
        print(format_str.format(file_name))
        output_file.writestr(file_name, contents)

    output_file.close()


def get_config_file(path):
    lines = []
    regex = re.compile("(password|passwd)\s*=\s*(\S+)")
    if os.path.isfile(path):
        try:
            file = open(path, 'r')
        except IOError as e:
            print("Error when opening file " + path + ": " + e.strerror)
        else:
            for line in file:
                # If the line looks like it contains a password, only print ***
                match = regex.search(line)
                if match:
                    pw_inds = [match.start(2), match.end(2)]
                    censored_line = line[0:(pw_inds[0])] + "***" + line[(pw_inds[1]):]
                    lines.append(censored_line)
                else:
                    lines.append(line)
            file.close()
    else:
        print("File " + path + " was not found.")

    return ''.join(lines)


def add_config_files_from_dir(directory_to_add, output_file):
    format_str = "Adding file {} to archive."
    if os.path.isdir(directory_to_add):
        for file in os.listdir(directory_to_add):
            if fnmatch.fnmatch(file, "*.cnf"):
                file_path = directory_to_add + file
                contents = get_config_file(file_path)
                if len(contents) > 0:
                    print(format_str.format(file_path))
                    output_file.writestr(file_path, contents)


def read_log_file(path):
    # Log files can be large, so only read at most N bytes from the end
    max_byte_count = 500000
    contents = ""
    if os.path.isfile(path):
        try:
            file = open(path, 'r')
        except IOError as e:
            print("Error when opening file " + path + ": " + e.strerror)
        else:
            if os.stat(path).st_size > max_byte_count:
                # Seek to the end, go back and find a newline
                file.seek(0, os.SEEK_END)
                file.seek(max(0, file.tell() - max_byte_count))
                file.readline()

            contents = file.read()
            file.close()
    else:
        print("File " + path + " was not found.")

    return contents


def run_max_ctrl(maxctrl_un, maxctrl_pw):
    maxctrl_commands = ["show maxscale", "show services", "show filters", "show monitors",
                        "show servers"]
    cmd_prefix = "maxctrl "
    if len(maxctrl_un) > 0:
        cmd_prefix += "--user={} --password={} ".format(maxctrl_un, maxctrl_pw)

    total_output = ""
    for command in maxctrl_commands:
        complete_cmd = cmd_prefix + command
        try:
            maxctrl_output_bytes = subprocess.check_output(complete_cmd, shell=True,
                                                           stderr=subprocess.PIPE)
        except subprocess.CalledProcessError as e:
            print("Error when calling maxctrl: command \"{}\" returned {}".format(complete_cmd, e.returncode))
            break  # If a command fails, stop trying
        except IOError as e:
            print("Error when calling maxctrl: {}".format(e.strerror))
            break
        else:
            if len(maxctrl_output_bytes) > 0:
                total_output += command + "\n" + maxctrl_output_bytes.decode("utf-8") + "\n"

    return total_output


def read_core_file():
    core_file_path = os.getcwd() + "/" + "core"
    core_file_contents = ""
    if os.path.isfile(core_file_path):
        print("Core file found, running gdb to save call stack.")
        gdb_command = "gdb --quiet -batch -ex \"thread apply all bt full\" -ex \"quit\" maxscale core"
        try:
            gdb_output_bytes = subprocess.check_output(gdb_command, shell=True, stderr=subprocess.PIPE)
        except subprocess.CalledProcessError as e:
            print("Error when calling gdb: command \"{}\" returned {}".format(gdb_command, e.returncode))
        except IOError as e:
            print("Error when calling gdb: {}".format(e.strerror))
        else:
            if len(gdb_output_bytes) > 0:
                core_file_contents += gdb_command + "\n\n" + gdb_output_bytes.decode("utf-8")

    return core_file_contents


if __name__ == "__main__":
    main(sys.argv)
