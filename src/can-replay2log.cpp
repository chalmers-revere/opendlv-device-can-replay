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

#include <iostream>
#include <iomanip>
#include <string>

int32_t main(int32_t argc, char **argv) {
    int32_t retCode{1};
    const std::string PROGRAM{argv[0]}; // NOLINT
    auto commandlineArguments = cluon::getCommandlineArguments(argc, argv);
    if ( (0 == commandlineArguments.count("can")) ) {
        std::cerr << argv[0] << " converts captured raw CAN frames in opendlv.proxy.RawUInt64CANFrame format into .asc format with the specified CAN device as prefix." << std::endl;
        std::cerr << "Usage:   " << argv[0] << " [--id=ID] --can=<name of the CAN device>" << std::endl;
        std::cerr << "         --id:     ID of opendlv.proxy.RawCANFrame to replay; default: 0" << std::endl;
        std::cerr << "Example: " << argv[0] << " --can=vcan0" << std::endl;
    }
    else {
        const std::string CANDEVICE{commandlineArguments["can"]};
        const uint32_t ID{(commandlineArguments["id"].size() != 0) ? static_cast<uint32_t>(std::stoi(commandlineArguments["id"])) : 0};

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

            while (player.hasMoreData() && !cluon::TerminateHandler::instance().isTerminated.load()) {
                auto next = player.getNextEnvelopeToBeReplayed();
                if (next.first) {
                    cluon::data::Envelope e = next.second;

                    if ( (e.dataType() == opendlv::proxy::RawUInt64CANFrame::ID()) && (e.senderStamp() == ID) ) {
                        canFrame = cluon::extractMessage<opendlv::proxy::RawUInt64CANFrame>(std::move(e));
                        if (0 < canFrame.length()) {
                            union CANData {
                                char bytes[8];
                                uint64_t value{0};
                            } canData;
                            canData.value = canFrame.data();
                            std::cout << "(" << e.sampleTimeStamp().seconds() << "."
                                      << std::setfill('0') << std::setw(6) << e.sampleTimeStamp().microseconds() << ")" 
                                      << " " << CANDEVICE << " ";
                            std::cout << std::setfill('0') << std::setw(0) << std::uppercase
                                      << std::hex << (canFrame.canID() & 0x3FFFFFFF)
                                      << "#";
                            std::cout << std::setfill('0') << std::setw(2) << std::uppercase;
                            for(uint8_t i{0}; i<canFrame.length(); i++) {
                                std::cout << std::hex << (+((uint8_t)canData.bytes[i]));
                            }
                            std::cout << std::dec << std::endl;
                        }
                    }
                }
            }
            retCode = 0;
        }
    }
    return retCode;
}

