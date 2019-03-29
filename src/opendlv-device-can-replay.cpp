/*
 * Copyright (C) 2019  Christian Berger
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

#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/select.h>

#ifdef __linux__
    #include <linux/if.h>
    #include <linux/can.h>
#endif

#include <unistd.h>

#include <cstring>

#include <iostream>
#include <string>

int32_t main(int32_t argc, char **argv) {
    int32_t retCode{1};
    const std::string PROGRAM{argv[0]}; // NOLINT
    auto commandlineArguments = cluon::getCommandlineArguments(argc, argv);
    if ( (0 == commandlineArguments.count("can")) ) {
        std::cerr << argv[0] << " replays captured raw CAN frames in opendlv.proxy.RawCANFrame format onto the selected CAN device." << std::endl;
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
#ifdef __linux__
            struct sockaddr_can address;
#endif
            int socketCAN;

            std::cerr << "[opendlv-device-vcan-replay] Opening " << CANDEVICE << "... ";
#ifdef __linux__
            // Create socket for SocketCAN.
            socketCAN = socket(PF_CAN, SOCK_RAW, CAN_RAW);
            if (socketCAN < 0) {
                std::cerr << "failed." << std::endl;

                std::cerr << "[opendlv-device-vcan-replay] Error while creating socket: " << strerror(errno) << std::endl;
            }

            // Try opening the given CAN device node.
            struct ifreq ifr;
            memset(&ifr, 0, sizeof(ifr));
            strcpy(ifr.ifr_name, CANDEVICE.c_str());
            if (0 != ioctl(socketCAN, SIOCGIFINDEX, &ifr)) {
                std::cerr << "failed." << std::endl;

                std::cerr << "[opendlv-device-vcan-replay] Error while getting index for " << CANDEVICE << ": " << strerror(errno) << std::endl;
                return retCode;
            }

            // Setup address and port.
            memset(&address, 0, sizeof(address));
            address.can_family = AF_CAN;
            address.can_ifindex = ifr.ifr_ifindex;

            if (bind(socketCAN, reinterpret_cast<struct sockaddr *>(&address), sizeof(address)) < 0) {
                std::cerr << "failed." << std::endl;

                std::cerr << "[opendlv-device-vcan-replay] Error while binding socket: " << strerror(errno) << std::endl;
                return retCode;
            }
            std::cerr << "done." << std::endl;
#else
            std::cerr << "failed (SocketCAN not available on this platform). " << std::endl;
            return retCode;
#endif

            int32_t nbytes{0};
            struct can_frame frame;
            opendlv::proxy::RawCANFrame canFrame;

            constexpr bool AUTOREWIND{false};
            constexpr bool THREADING{true};
            cluon::Player player(recFile, AUTOREWIND, THREADING);

            while (player.hasMoreData() && !cluon::TerminateHandler::instance().isTerminated.load()) {
                cluon::data::TimeStamp before = cluon::time::now();
                auto next = player.getNextEnvelopeToBeReplayed();
                if (next.first) {
                    cluon::data::Envelope e = next.second;

                    if ( (e.dataType() == opendlv::proxy::RawCANFrame::ID()) && (e.senderStamp() == ID) ) {
                        canFrame = cluon::extractMessage<opendlv::proxy::RawCANFrame>(std::move(e));
                        if ( (0 < canFrame.length()) && (-1 < socketCAN) ) {
#ifdef __linux__
                            frame.can_id = canFrame.canID();
                            frame.can_dlc = canFrame.length();
                            memcpy(frame.data, canFrame.data().data(), 8);
                            nbytes = ::write(socketCAN, &frame, sizeof(struct can_frame));
                            if (!(0 < nbytes)) {
                                std::clog << "[SocketCANDevice] Writing ID = " << frame.can_id << ", LEN = " << +frame.can_dlc << ", strerror(" << errno << "): '" << strerror(errno) << "'" << std::endl;
                            }
#endif
                        }
                    }
                    cluon::data::TimeStamp after = cluon::time::now();
                    int64_t delta{player.delay() - cluon::time::deltaInMicroseconds(after, before)};
                    if (0 < delta) {
                        std::this_thread::sleep_for(std::chrono::duration<int32_t, std::micro>(delta));
                    }
                }
            }

            std::clog << "[opendlv-device-vcan-replay] Closing " << CANDEVICE << "... ";
            if (socketCAN > -1) {
                close(socketCAN);
            }
            std::clog << "done." << std::endl;

            retCode = 0;
        }
    }
    return retCode;
}

