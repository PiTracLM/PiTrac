<!DOCTYPE html>
<html>
	<head>
		<meta charset="ISO-8859-1">
		<title>Welcome To The PiTrac Launch Monitor</title>

        <meta http-equiv="Cache-Control" content="no-cache, no-store, must-revalidate" />
        <meta http-equiv="Pragma" content="no-cache" />
        <meta http-equiv="Expires" content="0" />

<style>

    .grid-container {
        display: grid;
        grid-template-areas:
            'header header header header header header'
            'empty_row0 empty_row0 empty_row0 empty_row0 empty_row0 empty_row0'
            'speed_label speed_label speed_label carry_label carry_label carry_label'
            'speed speed speed carry carry carry'
            'empty_row1 empty_row1 empty_row1 empty_row1 empty_row1 empty_row1'
            'launch_angle_label launch_angle_label launch_angle_label side_angle_label side_angle_label side_angle_label'
            'launch_angle launch_angle launch_angle side_angle side_angle side_angle'
            'empty_row2 empty_row2 empty_row2 empty_row2 empty_row2 empty_row2'
            'back_spin_label back_spin_label back_spin_label side_spin_label side_spin_label side_spin_label '
            'back_spin back_spin back_spin side_spin side_spin side_spin '
            'empty_row3 empty_row3 empty_row3 empty_row3 empty_row3 empty_row3'
            'result_type result_type result_type result_type result_type result_type'
            'empty_row4 empty_row4 empty_row4 empty_row4 empty_row4 empty_row4'
            'message message message message message message'
            'empty_row5 empty_row5 empty_row5 empty_row5 empty_row5 empty_row5'
            'control_buttons control_buttons control_buttons control_buttons control_buttons control_buttons'
            'images images images images images images';
        row-gap: 0px;
        column-gap: 3px;
        font-family: Haettenschweiler, 'Arial', sans-serif;
        background-color: white
    }



.item1 { grid-area: header; }
.item52 { grid-area: carry_label; }
.item53 { grid-area: speed_label; }
.item2 { grid-area: carry; }
.item3 { grid-area: speed; }
.item54 { grid-area: launch_angle_label; }
.item55 { grid-area: side_angle_label; }
.item4 { grid-area: launch_angle; }
.item5 { grid-area: side_angle; }
.item56 { grid-area: back_spin_label; }
.item57 { grid-area: side_spin_label; }
.item6 { grid-area: back_spin; }
.item7 { grid-area: side_spin; }
.item8 { grid-area: result_type; }
.item9 { grid-area: message; }
.item10 { grid-area: images; }
.item11 { grid-area: control_buttons; }
.item100 { grid-area: empty_row0; }
.item101 { grid-area: empty_row1; }
.item102 { grid-area: empty_row2; }
.item103 { grid-area: empty_row3; }
.item104 { grid-area: empty_row4; }
.item105 { grid-area: empty_row5; }



    .grid-item-label {
        background-color: rgba(0, 0, 0, 0.8);
        border-top: 2px solid rgba(0, 0, 0, 0.8);
        border-top-left-radius: 10px;
        border-top-right-radius: 10px;
        border-bottom-left-radius: 0px;
        border-bottom-right-radius: 0px;
        border-left: 2px solid rgba(0, 0, 0, 0.8);
        border-right: 2px solid rgba(0, 0, 0, 0.8);
        text-align: center;
        padding: 6px;
        font-family: 'Arial', sans-serif;
        color: white;
        font-size: 22px;
    }



    .grid-item-value {
        background-color: rgba(255, 255, 255, 0.8);
        border-radius: 10px;
        border-top: 0px solid rgba(0, 0, 0, 0.8);
        border-bottom-left-radius: 10px;
        border-bottom-right-radius: 10px;
        border-top-left-radius: 0px;
        border-top-right-radius: 0px;
        border-left: 2px solid rgba(0, 0, 0, 0.8);
        border-right: 2px solid rgba(0, 0, 0, 0.8);
        border-bottom: 2px solid rgba(0, 0, 0, 0.8);
        padding: 10px;
        font-size: 40px;
        text-align: center;
    }


    .grid-item-text {
        background-color: rgba(255, 255, 255, 0.8);
        border: 2px solid rgb(0, 0, 0);
        border-width: medium;
        font-size: 30px;
        text-align: left;
        padding: 10px;
        padding-left: 20px;
        font-size: 30px;
        border-radius: 10px;
    }


    .grid-item-text-center-white {
        background-color: rgba(255, 255, 255, 0.8);
        border: 2px solid rgb(0, 0, 0);
        border-width: medium;
        font-size: 30px;
        text-align: center;
        padding: 10px;
        padding-left: 20px;
        font-size: 40px;
        border-radius: 10px;
    }

    .grid-item-text-center-green {
        background-color: #04AA6D;
        border: 2px solid rgb(0, 0, 0);
        border-width: medium;
        font-size: 30px;
        text-align: center;
        padding: 10px;
        padding-left: 20px;
        font-size: 40px;
        border-radius: 10px;
    }

    .grid-item-text-center-yellow {
        background-color: #FFFF00;
        border: 2px solid rgb(0, 0, 0);
        border-width: medium;
        font-size: 30px;
        text-align: center;
        padding: 10px;
        padding-left: 20px;
        font-size: 40px;
        border-radius: 10px;
    }

    .grid-item-empty-row {
        background-color: rgba(255, 255, 255, 0.8);
        text-align: center;
        padding: 1px;
        border-bottom-left-radius: 2px;
        border-bottom-right-radius: 2px;
        font-family: 'Arial', sans-serif;
        font-size: 20px;
    }


    log-text {
        text-align: left;
        font-size: 12px;
        font-family: 'Courier';
    }

</style>

</head>
    <body>

        <div class="grid-container">
            <div class=${ball_ready_color}>PiTrac Results</div>    <!-- TBD-  PiTrac -->
            <div class="item100 grid-item-empty-row"></div>
            <div class="item52 grid-item-label">Carry</div>
            <div class="item53 grid-item-label">Speed</div>
            <div class="item2 grid-item-value">${carry_yards}</div>
            <div class="item3 grid-item-value">${speed_mph}</div>
            <div class="item101 grid-item-empty-row"></div>
            <div class="item54 grid-item-label">Launch Angle</div>
            <div class="item55 grid-item-label">Side Angle</div>
            <div class="item4 grid-item-value">${launch_angle_deg}</div>
            <div class="item5 grid-item-value">${side_angle_deg}</div>
            <div class="item102 grid-item-empty-row"></div>
            <div class="item56 grid-item-label">Back Spin</div>
            <div class="item57 grid-item-label">Side Spin</div>
            <div class="item6 grid-item-value">${back_spin_rpm}</div>
            <div class="item7 grid-item-value">${side_spin_rpm}</div>
            <div class="item103 grid-item-empty-row"></div>
            <div class="item8 grid-item-text"><b>Result Type: </b>${result_type}</div>
            <div class="item104 grid-item-empty-row"></div>
            <div class="item9 grid-item-text"><b>Msg: </b>${message}</div>
            <div class="item105 grid-item-empty-row"></div>
            <div class="item10 grid-item-text"><b>Images:</b><br>${images}</div>
            <div class="item11 grid-item-text"><b>Controls:</b><br>${control_buttons}</div>
        </div>

        <br>
        ${log_messages}

    </body>
</html>
