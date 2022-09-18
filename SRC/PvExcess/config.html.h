const char sConfigPage[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>

<body onload="OnLoad()">

  <h2>Config PvExcess</h2>

  <form action="/save_new_config_data" method="get">
    <table border="0">
      <tr>
        <td> MQTT Server</td>
        <td><input id="server" name="server" type="text"></td>
      </tr>
      <tr>
        <td> MQTT Port</td>
        <td><input id="port" name="port" type="text"></td>
      </tr>
      <tr>
        <td>MQTT User</td>
        <td><input id="user" name="user" type="text"></td>
      </tr>
      <tr>
        <td>MQTT Pass</td>
        <td><input id="pass" name="pass" type="password"></td>
      </tr>
      <tr>
        <td>MQTT Topic</td>
        <td><input id="topic" name="topic" type="text"></td>
      </tr>
      <tr>
        <td>MQTT Key</td>
        <td><input id="key" name="key" type="text"></td>
      </tr>
    </table>
    <input type="submit" value="Save">
  </form>

<script>

  
  function OnLoad()
  {
    const obj = JSON.parse(CurrentValues);
    
    inputs = document.getElementsByTagName('input'); // Get no of input fielfs
    len = inputs.length - 1; // The subbmit button is also an input --> - 1

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
