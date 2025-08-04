package com.verdanttechs.jakarta.ee9;

import java.util.Vector;

import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;
import org.msgpack.core.MessagePack;
import org.msgpack.core.MessageBufferPacker;
import org.msgpack.core.MessageUnpacker;
import org.msgpack.value.ArrayValue;
import org.msgpack.value.Value;


import jakarta.servlet.ServletException;
import jakarta.servlet.annotation.WebServlet;
import jakarta.servlet.http.HttpServlet;
import jakarta.servlet.http.HttpSession;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;

import jakarta.jms.Connection;
import jakarta.jms.Session;
import jakarta.jms.Destination;
import jakarta.jms.Message;
import jakarta.jms.MessageProducer;
import jakarta.jms.MessageConsumer;
import jakarta.jms.DeliveryMode;
import jakarta.jms.TextMessage;
import jakarta.jms.BytesMessage;
import jakarta.jms.JMSException;
import jakarta.jms.ExceptionListener;

import java.io.FileReader;

import com.google.gson.Gson;
import com.google.gson.JsonParser;
import com.google.gson.JsonObject;
import com.google.gson.JsonElement;

import org.apache.activemq.ActiveMQConnectionFactory;

import java.io.IOException;


@WebServlet("/monitor")
public class MonitorServlet extends HttpServlet {

    private static Logger logger = LogManager.getLogger(MonitorServlet.class);

    public enum GsIPCResultType {
        kUnknown,
        kInitializing,
        kWaitingForBallToAppear,
        kWaitingForSimulatorArmed,
        kPausingForBallStabilization,
        kMultipleBallsPresent,
        kBallPlacedAndReadyForHit,
        kHit,
        kError,
        kCalibrationResults;
    }

    public enum IPCMessageType {
        kUnknown,
        kRequestForCamera2Image,
        kCamera2Image,
        kRequestForCamera2TestStillImage,
        kResults,
        kShutdown,
        kCamera2ReturnPreImage,
        kControlMessage;
    }

    public enum GsClubType {
        kNotSelected,
        kDriver,
        kIron,
        kPutter;
    }

    final static int kClubChangeToPutterControlMsgType = 1;
    final static int kClubChangeToDriverControlMsgType = 2;

    public static class GsControlMessage {
        public GsClubType club_type_ = GsClubType.kNotSelected;
    }

    public static GsControlMessage control_message_;
    public static boolean producer_created = false;
    public static MessageProducer producer;
    public static Connection producer_connection;
    public static ActiveMQConnectionFactory producer_connection_factory;
    public static Session producer_session;
    public static Destination producer_destination;

    public static void SetCurrentClubType(GsClubType club) {
        logger.info("SetClubType called with club type = " + String.valueOf(club));
        System.out.println("SetClubType called with club type = " + String.valueOf(club));

        try {
            if (!producer_created) {
                producer_connection_factory = new ActiveMQConnectionFactory(kWebActiveMQHostAddress);

                producer_connection = producer_connection_factory.createConnection();
                producer_connection.start();

                producer_session = producer_connection.createSession(false, Session.AUTO_ACKNOWLEDGE);

                producer_destination = producer_session.createTopic(kGolfSimTopic);

                producer = producer_session.createProducer(producer_destination);
                producer.setDeliveryMode(DeliveryMode.NON_PERSISTENT);

                producer_created = true;
            }

            BytesMessage bytesMessage = producer_session.createBytesMessage();


            bytesMessage.setStringProperty("Message Type", "GolfSimIPCMessage");
            bytesMessage.setIntProperty("IPCMessageType", 7 /*kControlMessage*/);
            bytesMessage.setStringProperty("LM_System_ID", "LM_GUI");

            MessageBufferPacker packer = MessagePack.newDefaultBufferPacker();

            int control_msg_type = 0;

            if (club == GsClubType.kPutter) {
                control_msg_type = kClubChangeToPutterControlMsgType;
            } else if (club == GsClubType.kIron) {
                // TBD - Not yet supported
            } else if (club == GsClubType.kDriver) {
                control_msg_type = kClubChangeToDriverControlMsgType;
            }

            packer
                    .packInt(control_msg_type);

            packer.close();

            final byte[] bytes = packer.toByteArray();

            bytesMessage.writeBytes(bytes);

            System.out.println("Created BytesMessage of size = " + String.valueOf(bytes.length));

            producer.send(bytesMessage);

            // Also set the current result object to have the same club type
            current_result_.club_type_ = club;

            }
            catch (Exception e) {
                logger.error("Exception publishing changed club event", e);
            }
    }

    public static class GsIPCResult {


        public int carry_meters_ = 0;
        public float speed_mpers_ = 0;
        public float launch_angle_deg_ = 0;
        public float side_angle_deg_ = 0;
        public int back_spin_rpm_ = 0;
        public int side_spin_rpm_ = 0;     // Negative is left (counter-clockwise from above ball)
        public int confidence_ = 0;
        public GsClubType club_type_ = GsClubType.kNotSelected;
        public GsIPCResultType result_type_ = GsIPCResultType.kUnknown;
        public String message_ = "No message set";
        public Vector<String> log_messages_ = new Vector<String>(20, 2);


        public static int MetersToYards(int meters) {
            return (int) ((3.281 / 3.0) * meters);
        }

        public static float MetersPerSecondToMPH(float speed_mpers) {
            return (float) (speed_mpers * 2.23694);
        }

        public static String FormatClubType(GsClubType club_type) {

            String s;

            switch (club_type) {
                case kDriver: {
                    s = "Driver";
                    break;
                }

                case kIron: {
                    s = "Iron";
                    break;
                }

                case kPutter: {
                    s = "Putter";
                    break;
                }

                default:
                case kNotSelected: {
                    s = "Not selected";
                    break;
                }
            }
            return s;
        }

        public String Format(HttpServletRequest request) {

            int carry_yards = MetersToYards(carry_meters_);
            float speed_mph = MetersPerSecondToMPH(speed_mpers_);

            String result_type = FormatResultType(result_type_);

            String carry_yards_str;
            String speed_mph_str;
            String launch_angle_deg_str;
            String side_angle_deg_str;
            String back_spin_rpm_str;
            String side_spin_rpm_str;
            String confidence_str;
            String club_str = "";
            String result_type_str;
            String message_str;
            String log_messages_str = "";
            String log_messages_console_str = "";
            String control_buttons_str;

            // System.out.println("Format called.");

            if (club_type_ != GsClubType.kNotSelected) {
                club_str = FormatClubType(club_type_);
            }

            if (Math.abs(speed_mph) > 0.001) {
                // Use calculated carry distance from trajectory calculator
                if (carry_yards > 0) {
                    carry_yards_str = String.valueOf(carry_yards) + " yards";
                } else {
                    carry_yards_str = "--";  // No carry data available
                }

                if (club_type_ != GsClubType.kPutter) {
                    int speed_mph_int = (int) speed_mph;
                    speed_mph_str = String.format("%.1f", speed_mph) + " mph";
                    launch_angle_deg_str = String.format("%.1f", launch_angle_deg_) + "&deg";
                } else {
                    speed_mph_str = String.format("%.2f", speed_mph) + " mph";
                    launch_angle_deg_str = "--";
                }
                side_angle_deg_str = String.format("%.1f", side_angle_deg_) + "&deg";

                // Spin doesn't really make sense for a putter
                if (club_type_ != GsClubType.kPutter && Math.abs(back_spin_rpm_) > 0.001) {
                    if (back_spin_rpm_ == 0.0 && side_spin_rpm_ == 0.0) {
                        back_spin_rpm_str = "N/A";
                        side_spin_rpm_str = "N/A";

                    }
                    if (Math.abs(back_spin_rpm_) < 100000) {
                        back_spin_rpm_str = String.valueOf(back_spin_rpm_) + " rpm";
                        String side_spin_direction = "L ";
                        if (side_spin_rpm_ < 0.0) {
                            side_spin_direction = "R ";
                        }
                        side_spin_rpm_str = side_spin_direction + String.valueOf(side_spin_rpm_) + " rpm";
                    } else {
                        back_spin_rpm_str = "err-out of range";
                        side_spin_rpm_str = "err-out of range";
                    }
                } else {
                    // System.out.println("Received back_spin_rpm_ = 0.");
                    back_spin_rpm_str = "--";
                    side_spin_rpm_str = "--";
                }
                confidence_str = String.valueOf(confidence_) + " rpm";
            } else {
                carry_yards_str = "--";
                speed_mph_str = "--";
                launch_angle_deg_str = "--";
                side_angle_deg_str = "--";
                back_spin_rpm_str = "--";
                side_spin_rpm_str = "--";
                result_type_str = "--";
                confidence_str = "--";
                message_str = "--";
            }

            // System.out.println("Format about to convert result_type.");

            if (result_type != "") {
                result_type_str = String.valueOf(result_type);
            } else {
                result_type_str = "--";
            }

            if (message_ != "") {
                message_str = String.valueOf(message_);
            } else {
                message_str = "--";
            }

            if (log_messages_.size() > 0) {
                log_messages_str = "<log-text>";
                log_messages_str += "<br><br>       <b>Log Messages:</b> <br>";

                for (int i = 0; i < log_messages_.size(); i++) {
                    log_messages_str += log_messages_.elementAt(i) + "<br>";
                    log_messages_console_str += log_messages_.elementAt(i) + "\n";
                }

                log_messages_str += "<\\log-text>";
            } else {
                log_messages_str = "--";
            }

            carry_yards_str += "  (" + club_str + ")";

            request.setAttribute("carry_yards", carry_yards_str);
            request.setAttribute("speed_mph", speed_mph_str);
            request.setAttribute("launch_angle_deg", launch_angle_deg_str);
            request.setAttribute("side_angle_deg", side_angle_deg_str);
            request.setAttribute("back_spin_rpm", back_spin_rpm_str);
            request.setAttribute("side_spin_rpm", side_spin_rpm_str);
            request.setAttribute("confidence", confidence_str);
            request.setAttribute("result_type", result_type_str);
            request.setAttribute("message", message_str);
            request.setAttribute("log_messages", log_messages_str);

            String s = "Carry: " + carry_yards_str + " yards." +
                    "              Speed: " + speed_mph_str + " mph.<br>" +
                    "       Launch Angle: " + launch_angle_deg_str + " degrees." +
                    "         Side Angle: " + side_angle_deg_str + " degrees.<br>" +
                    "          Back Spin: " + back_spin_rpm_str + " rpm." +
                    "          Side Spin: " + side_spin_rpm_str + " rpm.<br>" +
                    "         Confidence: " + confidence_str + " (0-10).<br>" +
                    "          Club Type: " + club_type_ + " 0-Unselected, 1-Driver, 2-Iron, 3-Putter\n" +
                    "        Result Type: " + result_type_str + "<br>" +
                    "            Message: " + message_str;

            if (log_messages_.size() > 0) {
                s += "       Log Messages:\n" + log_messages_console_str;
            }

            // Set the GUI top color to be white as a default.  Special cases follow
            request.setAttribute("ball_ready_color", "\"item1 grid-item-text-center-white\"");

            // System.out.println("Checking for Ball placed");

            if (result_type_ == GsIPCResultType.kBallPlacedAndReadyForHit) {
                System.out.println("Ball placed - setting ball_ready_color to 'item1 grid-item-text-center-green'");
                request.setAttribute("ball_ready_color", "\"item1 grid-item-text-center-green\"");
            }

            if (result_type_ == GsIPCResultType.kWaitingForSimulatorArmed) {
                System.out.println("kWaitingForSimulatorArmed - setting ball_ready_color to 'item1 grid-item-text-center-yellow'");
                request.setAttribute("ball_ready_color", "\"item1 grid-item-text-center-yellow\"");
            }

            // If the result is a hit, then include IMG images
            // Even if it's an error, we might still find the images (if they
            // exist) to be useful
            if (result_type_ == GsIPCResultType.kHit) {
                String images_string = "";

                if (club_type_ == GsClubType.kDriver) {
                    images_string += "<img src=\"" + kWebServerTomcatShareDirectory + "/" + kWebServerResultSpinBall1Image + "\" alt=\"1st Ball Image\" />" +
                            "<img src=\"" + kWebServerTomcatShareDirectory + "/" + kWebServerResultSpinBall2Image + "\" alt=\"2nd Ball Image\" />" +
                            "<img src=\"" + kWebServerTomcatShareDirectory + "/" + kWebServerResultBallRotatedByBestAngles + "\" alt=\"Ball1 Rotated by determined angles image\" />";
                }

                images_string += "<img src=\"" + kWebServerTomcatShareDirectory + "/" + kWebServerResultBallExposureCandidates + "\" alt=\"Identified Exposures Image\" width = \"720\" heigth=\"544\" />";

                request.setAttribute("images", images_string);
            } else if (result_type_ == GsIPCResultType.kError) {
                request.setAttribute("images", "<img src=\"" + kWebServerTomcatShareDirectory + "/" + kWebServerResultBallExposureCandidates + "\" alt=\"Identified Exposures Image\" width = \"720\" heigth=\"544\" />" +
                        "<img src=\"" + kWebServerTomcatShareDirectory + "/" + kWebServerErrorExposuresImage + "\" alt=\"Camera2 Image\" width = \"720\" heigth=\"544\" />");
            } else if (result_type_ == GsIPCResultType.kWaitingForBallToAppear) {
                // Make sure the user can see what the monitor is seeing, especially if the user may have placed the ball outside the ball search area
                request.setAttribute("images", "<img src=\"" + kWebServerTomcatShareDirectory + "/" + kWebServerBallSearchAreaImage + "\" alt=\"Ball Search Area\" width = \"360\" heigth=\"272\" />");
            }


            // control_buttons_str = "<button onclick=\"console.log('Putter Button Pressed.'); callPutterMode();\">Putter</button>";
            String driver_button_color_string;
            String putter_button_color_string;
            if (club_type_ != GsClubType.kPutter) {
                driver_button_color_string = "#04AA6D";  // Green
                putter_button_color_string = "#e7e7e7";  // Gray
            } else {
                driver_button_color_string = "#e7e7e7";  // Gray
                putter_button_color_string = "#04AA6D";  // Green
            }

            control_buttons_str = "<style> ";
            control_buttons_str += " input[value=\"Putter\"] { background-color: " + putter_button_color_string + ";} ";
            control_buttons_str += " input[value=\"Driver\"] { background-color: " + driver_button_color_string + ";} ";
            control_buttons_str += " </style>";

            control_buttons_str += "<form action=\"monitor\" method=\"post\"> " +
                    "<input type=\"submit\" name=\"driver\" value=\"Driver\" background-color: " + driver_button_color_string + "; style=\" font-family: 'Arial'; font-size:30px;\" />  " +
                    "<input type=\"submit\" name=\"putter\" value=\"Putter\" background-color: " + putter_button_color_string + "; style=\" font-family: 'Arial'; font-size:30px;\" />  " +
                    "<input type=\"submit\" name=\"P1\" value=\"P1\"  background-color: #04AA6D; style=\"font-family: 'Arial'; font-size:30px;\" />" +
                    "<input type=\"submit\" name=\"P2\" value=\"P2\" style=\"font-family: 'Arial'; font-size:30px;\" />" +
                    "<input type=\"submit\" name=\"P3\" value=\"P3\" style=\"font-family: 'Arial'; font-size:30px;\" />" +
                    "<input type=\"submit\" name=\"P4\" value=\"P4\" style=\"font-family: 'Arial'; font-size:30px;\" />" +
                    " </form>";
            request.setAttribute("control_buttons", control_buttons_str);


            return s;
        }

        public String FormatResultType(GsIPCResultType t) {
            String s = "NA";

            switch (t) {
                case kWaitingForBallToAppear: {
                    s = "Waiting for ball to appear in view frame";
                    break;
                }

                case kWaitingForSimulatorArmed: {
                    s = "Waiting for the simulator to be armed";
                    break;
                }

                case kInitializing: {
                    s = "Initializing launch monitor.";
                    break;
                }

                case kPausingForBallStabilization: {
                    s = "Pausing for the placed ball to stabilize";
                    break;
                }

                case kMultipleBallsPresent: {
                    s = "Muliple balls present";
                    break;
                }

                case kBallPlacedAndReadyForHit: {
                    s = "Ball placed and ready to be hit";
                    break;
                }

                case kHit: {
                    s = "Ball hit";
                    break;
                }

                case kError: {
                    s = "Error";
                    break;
                }

                case kUnknown: {
                    s = "Unknown";
                    break;
                }

                case kCalibrationResults: {
                    s = "CalibrationResults";
                    break;
                }

                default: {
                    s = "N/A (" + String.valueOf(t.ordinal()) + ")";
                    break;
                }
            }

            return s;
        }

        public boolean unpack(byte[] byteData) {

            MessageUnpacker unpacker = MessagePack.newDefaultUnpacker(byteData);

            // System.out.println("Unpacking Results Message");
            try {
                // Assume the unpacked data has a single, array value
                Value arrayValue = unpacker.unpackValue();

                ArrayValue a = arrayValue.asArrayValue();

                // TBD - Move to the IPCResult class
                carry_meters_ = a.get(0).asIntegerValue().toInt();

                switch (a.get(1).getValueType()) {
                    case INTEGER:
                        speed_mpers_ = a.get(1).asIntegerValue().toInt();
                        break;

                    case FLOAT:
                        speed_mpers_ = a.get(1).asFloatValue().toFloat();
                        break;

                    default:
                        System.out.println("Could not convert speed_mpers_ value");
                        break;
                }

                // System.out.println("Unpacked speed_mpers_");

                // The following overcomes apparent bug in msgpack where floats that don't have fractional parts
                // come across as integers
                switch (a.get(2).getValueType()) {
                    case INTEGER:
                        launch_angle_deg_ = a.get(2).asIntegerValue().toInt();
                        break;

                    case FLOAT:
                        launch_angle_deg_ = a.get(2).asFloatValue().toFloat();
                        break;

                    default:
                        System.out.println("Could not convert launch_angle_deg_ value");
                        break;
                }

                switch (a.get(3).getValueType()) {
                    case INTEGER:
                        side_angle_deg_ = a.get(3).asIntegerValue().toInt();
                        break;

                    case FLOAT:
                        side_angle_deg_ = a.get(3).asFloatValue().toFloat();
                        break;

                    default:
                        System.out.println("Could not convert side_angle_deg_ value");
                        break;
                }

                // System.out.println("Unpacking back_spin_rpm_.");

                back_spin_rpm_ = a.get(4).asIntegerValue().toInt();
                side_spin_rpm_ = a.get(5).asIntegerValue().toInt();
                confidence_ = a.get(6).asIntegerValue().toInt();

                // System.out.println("unpacked club type value: " + String.valueOf(a.get(7).asIntegerValue().toInt()));

                club_type_ = GsClubType.values()[a.get(7).asIntegerValue().toInt()];

                // System.out.println("unpacked club type: " + String.valueOf(club_type_));

                result_type_ = GsIPCResultType.values()[a.get(8).asIntegerValue().toInt()];

                if (!a.get(9).isNilValue()) {
                    message_ = a.get(9).asStringValue().toString();
                }

                // System.out.println("Unpacking log messages.");

                if (!a.get(10).isNilValue()) {

                    ArrayValue log_messages_value = a.get(10).asArrayValue();

                    log_messages_.clear();
                    for (int i = 0; i < log_messages_value.size(); i++) {
                        log_messages_.addElement(new String(log_messages_value.get(i).asStringValue().toString()));
                    }
                }

            } catch (Exception e) {
                System.out.println("Error occurred in unpack: " + e.getMessage());
                return false;
            }

            return true;
        }

    }

    // File paths appear to be relative to the home of the servlet.
    // E.g., /opt/tomee/webapps/golf_sim
    private static String kGolfSimConfigJsonFilename = "golf_sim_config.json";

    private static String kGolfSimTopic = "Golf.Sim";
    // Set from environment variable or default. This can be overidden from config file later
    private static String kWebActiveMQHostAddress = System.getenv("PITRAC_MSG_BROKER_FULL_ADDRESS") != null ?
            System.getenv("PITRAC_MSG_BROKER_FULL_ADDRESS") : "";
    private static String kWebServerTomcatShareDirectory;
    private static String kWebServerResultBallExposureCandidates;
    private static String kWebServerResultSpinBall1Image;
    private static String kWebServerResultSpinBall2Image;
    private static String kWebServerResultBallRotatedByBestAngles;
    private static String kWebServerErrorExposuresImage;
    private static String kWebServerBallSearchAreaImage;


    private static boolean monitorIsInitialized = false;
    private static boolean monitorIsRunning = true;
    private static boolean consumerIsCreated = false;

    private static boolean display_images = true;

    private static GsIPCResult current_result_ = new GsIPCResult();

    public static void thread(Runnable runnable, boolean daemon) {
        Thread brokerThread = new Thread(runnable);
        brokerThread.setDaemon(daemon);
        brokerThread.start();
    }


    public boolean initializeMonitor(String config_filename) {
        if (monitorIsInitialized) {
            return true;
        }

        // The config filename comes from the request, e.g., 
        // http://rsp02:8080/golfsim/monitor?config_filename="%2Fmnt%2FVerdantShare%2Fdev%2FWebShare%2Fgolf_sim_config.json"
        Gson gson = new Gson();

        // The monitor isn't up and running yet.  Initialize

        try {

            FileReader reader = new FileReader(config_filename);

            JsonElement jsonElement = JsonParser.parseReader(reader);
            JsonObject jsonObject = jsonElement.getAsJsonObject();

            JsonElement gsConfigElement = jsonObject.get("gs_config");
            JsonElement ipcInterfaceElement = gsConfigElement.getAsJsonObject().get("ipc_interface");
            JsonElement userInterfaceElement = gsConfigElement.getAsJsonObject().get("user_interface");

            String pngSuffix = new String(".png");

            JsonElement kWebServerTomcatShareDirectoryElement = userInterfaceElement.getAsJsonObject().get("kWebServerTomcatShareDirectory");
            JsonElement kWebServerResultBallExposureCandidatesElement = userInterfaceElement.getAsJsonObject().get("kWebServerResultBallExposureCandidates");
            JsonElement kWebServerResultSpinBall1ImageElement = userInterfaceElement.getAsJsonObject().get("kWebServerResultSpinBall1Image");
            JsonElement kWebServerResultSpinBall2ImageElement = userInterfaceElement.getAsJsonObject().get("kWebServerResultSpinBall2Image");
            JsonElement kWebServerResultBallRotatedByBestAnglesElement = userInterfaceElement.getAsJsonObject().get("kWebServerResultBallRotatedByBestAngles");
            JsonElement kWebServerErrorExposuresImageElement = userInterfaceElement.getAsJsonObject().get("kWebServerErrorExposuresImage");
            JsonElement kWebServerBallSearchAreaImageElement = userInterfaceElement.getAsJsonObject().get("kWebServerBallSearchAreaImage");
            JsonElement kRefreshTimeSecondsElement = userInterfaceElement.getAsJsonObject().get("kRefreshTimeSeconds");

            if (kWebActiveMQHostAddress.isEmpty()) {
                kWebActiveMQHostAddress = ipcInterfaceElement.getAsJsonObject().get("kWebActiveMQHostAddress").getAsString();
            }

            kWebServerTomcatShareDirectory = (String) kWebServerTomcatShareDirectoryElement.getAsString();
            kWebServerResultBallExposureCandidates = (String) kWebServerResultBallExposureCandidatesElement.getAsString() + pngSuffix;
            kWebServerResultSpinBall1Image = (String) kWebServerResultSpinBall1ImageElement.getAsString() + pngSuffix;
            kWebServerResultSpinBall2Image = (String) kWebServerResultSpinBall2ImageElement.getAsString() + pngSuffix;
            kWebServerResultBallRotatedByBestAngles = (String) kWebServerResultBallRotatedByBestAnglesElement.getAsString() + pngSuffix;
            kWebServerErrorExposuresImage = (String) kWebServerErrorExposuresImageElement.getAsString() + pngSuffix;
            kWebServerBallSearchAreaImage = (String) kWebServerBallSearchAreaImageElement.getAsString() + pngSuffix;
            kRefreshTimeSeconds = (int) kRefreshTimeSecondsElement.getAsInt();

        } catch (Exception e) {
            System.out.println("Failed to parse JSON config file: " + e.getMessage());
            return false;
        }
        System.out.println("Golf Sim Configuration Settings: ");
        System.out.println("  kWebActiveMQHostAddress: " + kWebActiveMQHostAddress);
        System.out.println("  kWebServerTomcatShareDirectory (NOTE - Must be setup in Tomcat's conf/server.xml): " + kWebServerTomcatShareDirectory);
        System.out.println("  kWebServerResultBallExposureCandidates " + kWebServerResultBallExposureCandidates);
        System.out.println("  kWebServerResultSpinBall1Image " + kWebServerResultSpinBall1Image);
        System.out.println("  kWebServerResultSpinBall2Image " + kWebServerResultSpinBall2Image);
        System.out.println("  kWebServerResultBallRotatedByBestAngles " + kWebServerResultBallRotatedByBestAngles);
        System.out.println("  kWebServerErrorExposuresImage " + kWebServerErrorExposuresImage);
        System.out.println("  kWebServerBallSearchAreaImage " + kWebServerBallSearchAreaImage);


        monitorIsInitialized = true;
        return true;
    }

    private static boolean haveValidHitResult = false;

    private static int kRefreshTimeSeconds = 2;
    private static int max_time_to_reset_seconds = 60;
    private static int time_since_last_reset_seconds = 0;


    @Override
    protected void doPost(HttpServletRequest request, HttpServletResponse response) throws ServletException, IOException {

        System.out.println("doPost called.");

        GsIPCResult ipc_result = new GsIPCResult();

        if (request.getParameter("putter") != null) {
            SetCurrentClubType(GsClubType.kPutter);
        } else if (request.getParameter("driver") != null) {
            SetCurrentClubType(GsClubType.kDriver);
        } else {
            System.out.println("doPost received unknown request parameter.");
        }


        response.addHeader("Refresh", String.valueOf(0));

        response.setContentType("text/html");

        request.getRequestDispatcher("/WEB-INF/gs_dashboard.jsp").forward(request, response);
    }

    @Override
    protected void doGet(HttpServletRequest request, HttpServletResponse response)
            throws ServletException, IOException {


        String config_filename = request.getParameter("config_filename");
        String display_images_str = request.getParameter("display_images");
        // System.out.println("config_filename: " + config_filename);
        // System.out.println("display_images_str: " + display_images_str);

        if (display_images_str == "0") {
            display_images = false;
        } else {
            display_images = true;
        }

        if (!initializeMonitor(config_filename)) {
            System.out.println("Failed to initialize the monitor.");
        }

        Runtime.getRuntime().addShutdownHook(new Thread() {
            public void run() {
                monitorIsRunning = false;
                System.out.println("Caught contextDestroyed - shutting down the monitor.");
            }
        });


        try {
            if (!consumerIsCreated) {


                // Create a ConnectionFactory
                ActiveMQConnectionFactory connectionFactory = new ActiveMQConnectionFactory(kWebActiveMQHostAddress);

                // Create a PooledConnectionFactory
                // PooledConnectionFactory pooledConnectionFactory = new PooledConnectionFactory(connectionFactory);
                // ConnectionFactory connectionFactory = new connectionFactory(connectionFactory);

                // Connection connection = connectionFactory.createConnection();
                Connection connection = connectionFactory.createConnection();
                connection.start();

                // Create a Session
                Session session = connection.createSession(false, Session.AUTO_ACKNOWLEDGE);

                Destination destination = session.createTopic(kGolfSimTopic);

                // Create a MessageProducer from the Session to the Topic or Queue
                /* TBD - we are not producing any messages yet
                MessageProducer producer = session.createProducer(destination);
                producer.setDeliveryMode(DeliveryMode.NON_PERSISTENT);
                // Tell the producer to send the message
                // producer.send(message);

                // Create a message
                TextMessage message = session.createTextMessage("Hello ActiveMQ World!");
                */

                // Start a thread to try to receive the message
                System.out.println("Started consumer.");
                thread(new GolfSimConsumer(), false);
                consumerIsCreated = true;
                // Give the consumer a chance to wake up
                Thread.sleep(500);
            }

            HttpSession httpSession = request.getSession();
            Long times = (Long) httpSession.getAttribute("times");
            if (times == null) {
                httpSession.setAttribute("times", 0L);
            }

            long value = 1;
            if (times != null) {
                value = (times.longValue()) + 1;
            }

            time_since_last_reset_seconds += kRefreshTimeSeconds;

            if (time_since_last_reset_seconds > max_time_to_reset_seconds) {
                time_since_last_reset_seconds = 0;

                // Reset the result so we're not still looking at the results
                // of an old message
                current_result_ = new GsIPCResult();
            }

            response.addHeader("Refresh", String.valueOf(kRefreshTimeSeconds));

            response.setContentType("text/html");

            String debugDashboard = current_result_.Format(request);

            request.getRequestDispatcher("/WEB-INF/gs_dashboard.jsp").forward(request, response);

            // System.out.println("Received data:\n" + debugDashboard);

        } catch (Exception e) {
            response.getOutputStream().println(e.getMessage());
            return;
        }

        response.getOutputStream().println("Started ActiveMQ messaging.");
        System.out.println("Sent ActiveMQ message (system out).!");


    }

    private static class GolfSimConsumer implements Runnable, ExceptionListener {
        public void run() {

            try {

                System.out.println("GolfSimConsumer started.");
                ActiveMQConnectionFactory connectionFactory = new ActiveMQConnectionFactory(kWebActiveMQHostAddress);

                Connection connection = connectionFactory.createConnection();
                connection.start();

                // Create a Session
                Session session = connection.createSession(false, Session.AUTO_ACKNOWLEDGE);

                // Create the destination (Topic or Queue)
                Destination destination = session.createTopic(kGolfSimTopic);

                // Create a MessageConsumer from the Session to the Topic or Queue
                MessageConsumer consumer = session.createConsumer(destination);

                System.out.println("Waiting to receive message...");

                while (monitorIsRunning) {

                    // Wait for a message - time is in ms
                    // TBD - should we use a queue so as not to miss anything bewtwen waits?
                    Message message = consumer.receive(100);

                    if (message == null) {
                        continue;
                    } else if (message instanceof TextMessage) {
                        TextMessage textMessage = (TextMessage) message;
                        String text = textMessage.getText();
                        System.out.println("Received TextMessage: " + text);
                    } else if (message instanceof BytesMessage) {
                        BytesMessage bytesMessage = (BytesMessage) message;
                        long length = bytesMessage.getBodyLength();
                        // System.out.println("Received BytesMessage: " + String.valueOf(length) + " bytes.");

                        // We should never (currently) be getting a message this large.  Ignore it if we do.
                        if (length > 10000) {
                            continue;
                        }

                        byte[] byteData = null;
                        byteData = new byte[(int) length];
                        bytesMessage.readBytes(byteData);

                        // System.out.println("Received BytesMessage");

                        // Make sure this is a result type message, and not something like
                        // a Cam2Image
                        int gs_ipc_message_type_tag = bytesMessage.getIntProperty("IPCMessageType");

                        if (gs_ipc_message_type_tag != 4 /* kResults */) {
                            System.out.println("Received ByesMessage of IPCMessageType: " + String.valueOf(gs_ipc_message_type_tag));
                            continue;
                        }

                        // Reset the timer because we have a new message
                        time_since_last_reset_seconds = 0;

                        GsIPCResult new_result = new GsIPCResult();
                        new_result.unpack(byteData);

                        //System.out.println("Received new result message club type of: " + String.valueOf(new_result.club_type_));

                        // Always update the club type.  This new result
                        // may have been sent only to update that.
                        current_result_.club_type_ = new_result.club_type_;

                        // Only replace the current hit result (if we have one) if the ball has been re-teed up
                        if (current_result_.speed_mpers_ > 0 || current_result_.result_type_ == GsIPCResultType.kError) {
                            // We may not want to update the screen just yet if it has useful information and
                            // the incoming result record is not very interesting
                            if (new_result.speed_mpers_ <= 0 &&
                                    (new_result.result_type_ != GsIPCResultType.kBallPlacedAndReadyForHit &&
                                            new_result.result_type_ != GsIPCResultType.kInitializing &&
                                            new_result.result_type_ != GsIPCResultType.kWaitingForSimulatorArmed &&
                                            new_result.result_type_ != GsIPCResultType.kError
                                    )) {
                                // Don't replace the current result, as the user may still be looking at it
                            } else {
                                // A new ball has been teed up, so show the new status
                                current_result_.unpack(byteData);
                            }
                        } else {
                            // We don't appear to have any prior hit result data
                            current_result_.unpack(byteData);
                        }
                    } else {
                        System.out.println("Received unknown message type: " + message);
                    }
                }
                consumer.close();
                session.close();
                connection.close();

            } catch (Exception e) {
                System.out.println("Caught: " + e);
                e.printStackTrace();
            }
        }

        public synchronized void onException(JMSException ex) {
            System.out.println("JMS Exception occured.  Shutting down client.");
        }
    }


}

