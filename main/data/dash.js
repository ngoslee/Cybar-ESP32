'use strict';
var ws = new WebSocket('ws://' + location.host + '/ws');
let btn_states = new Array(6).fill(-1);

function sendChange(type, id, value) {
    console.log('sendChange event %s %d %d', type, id, value);
    let fval = 0;
    let cid = 0;
    if (type == "btn") {
        if (id >0 && id <=9) {
            cid= id -1;
            let curr_state = btn_states[cid];
            let new_state = curr_state;
            if (value == -1) {
                switch (curr_state) {
                    case -1:
                        new_state = 1;
                        break;  
                    case 1:
                        new_state = 0;
                        break;
                    case 0:
                        new_state = -1;
                        break;
                    default:
                        new_state = -1;
                        break;
                }
            }

            btn_states[cid] = new_state;
            let fid = 'btn' + (id);
//            console.log("id: "+fid);
            let btn = document.getElementById(fid);
 //           console.log('button: ' + btn);
            if (new_state == 1) {
                btn.classList.add('on');
                btn.classList.remove('off');
            } else if (new_state == 0) {
                btn.classList.remove('on');
                btn.classList.add('off');
            } else {
                btn.classList.remove('on');
                btn.classList.remove('off');
            }
            let msg = JSON.stringify({type: type, id: cid, value: parseInt(new_state)});
            ws.send(msg);
            console.log(msg)
        }
    }
}
ws.onmessage = function(evt) {
    var data = JSON.parse(evt.data);
    if (data.type === 'set_slider') {
    document.getElementById('slider' + data.id).value = data.value;
    } else if (data.type === 'set_switch') {
    document.getElementById('switch' + data.id).checked = data.value > 0;
    } else if (data.type === 'set_indicator') {
    document.getElementById('ind' + data.id).style.opacity = data.value / 100;
    }
};
ws.onclose = function() {
    console.log('WebSocket connection closed');
};