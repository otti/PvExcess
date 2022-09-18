const char sConfigPage[] PROGMEM = R"=====(
<!DOCTYPE html>
<html>

<body onload="LoadInputFields()">

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

  function LoadInputFields()
  {
    const obj = JSON.parse(CurrentValues);
    
    document.getElementById('server').value = obj.server;
    document.getElementById('port').value   = obj.port;
    document.getElementById('user').value   = obj.user;
    document.getElementById('pass').value   = obj.pass;
    document.getElementById('topic').value  = obj.topic;
    document.getElementById('key').value    = obj.key;
  
  }

  </script>

</body>
</html>
)=====";
