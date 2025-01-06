package com.verdanttechs.jakarta.ee9;

import jakarta.servlet.ServletException;
import jakarta.servlet.ServletOutputStream;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import java.io.IOException;
import java.io.PrintWriter;

import static org.mockito.Mockito.*;

class HelloServletTest {
    private HelloServlet helloServlet;
    private HttpServletRequest request;
    private HttpServletResponse response;
    private PrintWriter writer;
    private ServletOutputStream outputStream;

    @BeforeEach
    void setUp() throws IOException {
        response = mock(HttpServletResponse.class);
        outputStream = mock(ServletOutputStream.class);
        writer = mock(PrintWriter.class);
        when(response.getOutputStream()).thenReturn(outputStream);
        when(response.getWriter()).thenReturn(writer);
        helloServlet = new HelloServlet();
        request = mock(HttpServletRequest.class);
    }



    @Test
    void doGet_returnsTestMonitorCompleted() throws ServletException, IOException {
        helloServlet.doGet(request, response);
        verify(outputStream).println("Test Monitor Completed.");
    }

}