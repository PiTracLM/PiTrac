/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

#include <iostream>
#include <memory>
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/enable_shared_from_this.hpp>

#include "logging_tools.h"
#include "gs_options.h"
#include "gs_config.h"

#include "gs_gspro_test_server.h"


using boost::asio::ip::tcp; 


namespace golf_sim {

    GsGSProConnection::pointer GsGSProConnection::Create(boost::asio::io_context& io_context, int port_number)
    {
        return pointer(new GsGSProConnection(io_context, port_number));
    }

    tcp::socket& GsGSProConnection::GsGSProConnection::GetSocket()
    {
        return socket_;
    }

    std::string GsGSProConnection::GenerateResponseString()
    {
        std::string response_string = "{\n\
        \"Code\": 201,   \
        \"Message\" : \"GSPro Player Information\",   \
        \"Player\" : {\n   \
        \"Handed\": \"RH\",\n   \
        \"Club\" : \"DR\"\n   \
        }  }";

        return response_string;
    }

    void GsGSProConnection::Start()
    {
        std::array<char, 2000> buf;
        boost::system::error_code error;


        std::string buffer_string;

        while (true) {
            GS_LOG_TRACE_MSG(trace, "About to read data.");
            size_t len = socket_.read_some(boost::asio::buffer(buf), error);
            if (len == 0) {
                GS_LOG_MSG(warning, "Received a 0-length string from GSPro server.");
            }
            else {
                // Null-terminate the string
                buf[len] = (char)0;
                buffer_string += std::string(buf.data());
                GS_LOG_TRACE_MSG(trace, "   Read some data (" + std::to_string(len) + " bytes) : " + buffer_string);
            }

            if (error == boost::asio::error::eof) {
                GS_LOG_MSG(error, "Received unexpected EOF from the Launch Monitor.");
                return; // Connection closed cleanly by peer.
            }
            else if (error) {
                GS_LOG_MSG(error, "Received unexpected error from the Launch Monitor.");
                throw boost::system::system_error(error); // Some other error.
            }

            GS_LOG_TRACE_MSG(trace, "Received the following message from the Launch Monitor: " + buffer_string);

            message_ = GenerateResponseString();

            GS_LOG_TRACE_MSG(trace, "Sending the following message from the GSPro simulated server: " + message_);
            boost::asio::async_write(socket_, boost::asio::buffer(message_),
                boost::bind(&GsGSProConnection::HandleWrite, shared_from_this(),
                    boost::asio::placeholders::error,
                    boost::asio::placeholders::bytes_transferred));
        }
    }

    GsGSProConnection::GsGSProConnection(boost::asio::io_context& io_context, int port_number)
        : socket_(io_context)
    {
    }

    void GsGSProConnection::HandleWrite(const boost::system::error_code& error,
        size_t bytes_transferred) {
        GS_LOG_TRACE_MSG(trace, "bytes_transferred: " + std::to_string(bytes_transferred));
    }


//================== NEXT CLASS HERE ====================


    GsGSProTestServer::GsGSProTestServer(boost::asio::io_context& io_context, int port_number) : io_context_(io_context),
                                    acceptor_(io_context, tcp::endpoint(tcp::v4(), port_number))
    {
        port_number_ = port_number;
        StartAccept();
    }

    void GsGSProTestServer::StartAccept()
    {
        GS_LOG_TRACE_MSG(trace, "GsGSProTestServer::StartAccept.  port_number_: " + std::to_string(port_number_));

        GsGSProConnection::pointer new_connection =
            GsGSProConnection::Create(io_context_, port_number_);

        acceptor_.async_accept(new_connection->GetSocket(),
            boost::bind(&GsGSProTestServer::HandleAccept, this, new_connection,
                boost::asio::placeholders::error));
    }

    void GsGSProTestServer::HandleAccept(GsGSProConnection::pointer new_connection,
        const boost::system::error_code& error)
    {
        if (!error)
        {
            new_connection->Start();
        }

        StartAccept();
    }


}