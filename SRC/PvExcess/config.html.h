const char sConfigPage[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>

<body onload="OnLoad()">

  <h2>Config PvExcess</h2>

  <form action="/save_new_config_data" method="get">  
    MQTT Server<br>
    <input id="server" name="server" type="text"><br><br>

    MQTT Port<br>
    <input id="port" name="port" type="text"><br><br>

    MQTT User<br>
    <input id="user" name="user" type="text"><br><br>

    MQTT Pass<br>
    <input id="pass" name="pass" type="password"><br><br>

    MQTT Topic<br>
    <input id="topic" name="topic" type="text"><br><br>

    MQTT Key<br>
    <input id="key" name="key" type="text"><br><br>

    Power needed by appliance [W]<br>
    <input id="power" name="power" type="text"><br><br>

    Time to wait before start [s]<br>
    <input id="time" name="time" type="text"><br><br>
    
    <input type="submit" value="Save">
  </form>

<script>

  function OnLoad()
  {
    const obj = JSON.parse(CurrentValues);
    
    inputs = document.getElementsByTagName('input'); // Get no of input fields
    len = inputs.length - 1; // The submit button is also an input --> - 1

    var i = 0;
    for( const x in obj)
    {
      inputs[i].value = obj[x];
      i++;
    }
  }

  </script>

</body>
</html>
)=====";
