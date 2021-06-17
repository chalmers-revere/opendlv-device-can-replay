/*
 * Copyright (C) 2021  Christian Berger
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "cluon-complete.hpp"
#include "can.hpp"

#include <unistd.h>

#include <cstring>
#include <ctime>
#include <iostream>
#include <iomanip>
#include <locale>
#include <string>

int32_t main(int32_t argc, char **argv) {
    int32_t retCode{1};
    const std::string PROGRAM{argv[0]}; // NOLINT
    auto commandlineArguments = cluon::getCommandlineArguments(argc, argv);
    if ( (0 == commandlineArguments.count("log")) && (0 == commandlineArguments.count("asc")) ) {
        std::cerr << argv[0] << " converts captured raw CAN frames in opendlv.proxy.RawUInt64CANFrame format into .asc or .log format with the specified CAN device as prefix." << std::endl;
        std::cerr << "Usage:   " << argv[0] << " [--id=ID] --log" << std::endl;
        std::cerr << "         --asc:    convert RawUInt64CANFrame into .asc format" << std::endl;
        std::cerr << "         --log:    convert RawUInt64CANFrame into .log format" << std::endl;
        std::cerr << "         --id:     ID of opendlv.proxy.RawCANFrame to replay; default: -1 to export all CAN frames" << std::endl;
        std::cerr << "Example: " << argv[0] << " --log" << std::endl;
    }
    else {
        const int32_t ID{(commandlineArguments["id"].size() != 0) ? static_cast<int32_t>(std::stoi(commandlineArguments["id"])) : -1};
        const bool ASC{(commandlineArguments["asc"].size() != 0)};
        const bool LOG{(commandlineArguments["log"].size() != 0)};

        std::string recFile;
        for (auto e : commandlineArguments) {
            if (e.second.empty() && e.first != PROGRAM) {
                recFile = e.first;
                break;
            }
        }

        std::fstream fin(recFile, std::ios::in|std::ios::binary);
        if (!recFile.empty() && fin.good()) {
            fin.close();

            opendlv::proxy::RawUInt64CANFrame canFrame;

            constexpr bool AUTOREWIND{false};
            constexpr bool THREADING{false};
            cluon::Player player(recFile, AUTOREWIND, THREADING);

            cluon::data::TimeStamp first;
            while (player.hasMoreData() && !cluon::TerminateHandler::instance().isTerminated.load()) {
                auto next = player.getNextEnvelopeToBeReplayed();
                if (next.first) {
                    cluon::data::Envelope e = next.second;

                    if ( (e.dataType() == opendlv::proxy::RawUInt64CANFrame::ID()) && ((-1 == ID) || (static_cast<int32_t>(e.senderStamp()) == ID)) ) {
                        if ( 0 == (first.seconds() + first.microseconds()) ) {
                            first = e.sampleTimeStamp();
                            if (ASC) {
                                std::time_t t = static_cast<std::time_t>(first.seconds());
                                char mbstr[100];
                                if (std::strftime(mbstr, sizeof(mbstr), "%a %b %d %r %G", std::localtime(&t))) {
                                    std::cout << "date " << mbstr << std::endl;
                                    std::cout << "base hex  timestamps absolute" << std::endl;
                                    std::cout << "internal events logged" << std::endl;
                                    std::cout << "// version 10.0.1" << std::endl;
                                }
                            }
                        }
                        canFrame = cluon::extractMessage<opendlv::proxy::RawUInt64CANFrame>(std::move(e));
                        if (0 < canFrame.length()) {
                            union CANData {
                                char bytes[8];
                                uint64_t value{0};
                            } canData;
                            canData.value = canFrame.data();
                            if (LOG) { 
                                std::cout << "(" << e.sampleTimeStamp().seconds() << "."
                                          << std::setfill('0') << std::setw(6) << e.sampleTimeStamp().microseconds() << ")" 
                                          << " can" << e.senderStamp() << " ";
                                std::cout << std::setfill('0') << std::setw(0) << std::uppercase
                                          << std::hex << (canFrame.canID() & 0x3FFFFFFF)
                                          << "#";
                                std::cout << std::setfill('0') << std::setw(2) << std::uppercase;
                                for(uint8_t i{0}; i<canFrame.length(); i++) {
                                    std::cout << std::hex << std::setfill('0') <<  std::setw(2) << (+((uint8_t)canData.bytes[i]));
                                }
                                std::cout << std::dec << std::endl;
                            }
                            if (ASC) {
                                std::cout << "   " << (e.sampleTimeStamp().seconds() - first.seconds()) << "."
                                          << std::setfill('0') << std::setw(6) << e.sampleTimeStamp().microseconds() << " " 
                                          << " " << e.senderStamp() << "  ";
                                std::cout << std::setfill('0') << std::setw(0) << std::uppercase
                                          << std::hex << (canFrame.canID() & 0x3FFFFFFF) << "x";
                                std::cout << std::dec;
                                std::cout << std::setfill(' ') << std::setw(0);
                                std::cout << "\t\tRx   d " << std::dec << (+canFrame.length()) << " ";
                                for(uint8_t i{0}; i<canFrame.length(); i++) {
                                    std::cout << std::hex << std::setfill('0') <<  std::setw(2) << (+((uint8_t)canData.bytes[i])) << " ";
                                }
                                std::cout << " Length = 0 BitCount = 0 ID = " << std::dec << canFrame.canID() << "x" << std::endl;
                            }
                        }
                    }
                }
            }
            retCode = 0;
        }
    }
    return retCode;
}

