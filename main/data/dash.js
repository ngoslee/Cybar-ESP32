'use strict';
var ws = new WebSocket('ws://' + location.host + '/ws');
let btn_states = new Array(9).fill(-1);

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
    } else if (type == "slider") {
        if (id >=0 && id <6) {
            let msg = JSON.stringify({type: type, id: id, value: parseInt(value)});
            ws.send(msg);
            console.log(msg);   
        }  
    } else if (type == "mode")   {
        if (id >=0 && id<=3){
            let ctl = document.getElementById("mode" + (id));
            let wigwag = document.getElementById("mode1");
            let seek = document.getElementById("mode2");
            let scan = document.getElementById("mode3");

            switch (id) {
                case 0:
                    wigwag.classList.remove("on");
                    seek.classList.remove("on");
                    scan.classList.remove("on");
                    break;
                case 1:
                    wigwag.classList.add("on");
                    seek.classList.remove("on");
                    scan.classList.remove("on");
                    break;
                case 2:
                    wigwag.classList.remove("on");
                    seek.classList.add("on");
                    scan.classList.remove("on");
                    break;
                case 3:
                    wigwag.classList.remove("on");
                    seek.classList.remove("on");
                    scan.classList.add("on");
                    break;
                default:
                    wigwag.classList.remove("on");
                    seek.classList.remove("on");
                    scan.classList.remove("on");
                    break;
            }
            let msg = JSON.stringify({type: type, id: id, value: parseInt(value)});
            ws.send(msg);
            console.log(msg);   
        }
    } else {
        console.log("Unhandled");
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