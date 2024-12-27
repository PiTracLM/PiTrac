/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022-2025, Verdant Consultants, LLC.
 */

// Just for testing.  This class simulates the GSPro golf simulator interface.

#pragma once

#include <string>
#include <iostream>
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>

using boost::asio::ip::tcp;


namespace golf_sim {

    class GsGSProConnection : public std::enable_shared_from_this<GsGSProConnection>
    {
    public:
        typedef std::shared_ptr<GsGSProConnection> pointer;

        static pointer Create(boost::asio::io_context& io_context, int port_number);

        tcp::socket& GetSocket();

        std::string GenerateResponseString();

        void Start();

    private:
        GsGSProConnection(boost::asio::io_context& io_context, int port_number);

        void HandleWrite(const boost::system::error_code& error, size_t bytes_transferred);

        tcp::socket socket_;
        std::string message_;
    };

    class GsGSProTestServer
    {
    public:
        GsGSProTestServer(boost::asio::io_context& io_context, int port_number);

    private:
        void StartAccept();

        void HandleAccept(GsGSProConnection::pointer new_connection,
            const boost::system::error_code& error);
            
        boost::asio::io_context& io_context_;
        int port_number_ = 0;
        tcp::acceptor acceptor_;
    };

}