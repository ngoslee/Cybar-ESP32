let ws = new WebSocket(`ws://${location.host}/ws`);

ws.onopen = function() {
    console.log("WebSocket connected");
    ws.send("init");
};

ws.onerror = function(error) {
    console.error("WebSocket error:", error);
};

ws.onclose = function(event) {
    console.log("WebSocket closed:", event);
};

function sendChange(type, id, value) {
    ws.send(JSON.stringify({type: type, id: id, value: parseInt(value)}));
}

ws.onmessage = function(event) {
    console.log("Received:", event.data);
    try {
        let data = JSON.parse(event.data);
        if (data.states) {
            for (let i = 0; i < 6; i++) {
                let btn = document.getElementById(`btn${i + 1}`);
                if (data.states[i]) {
                    btn.classList.add('on');
                } else {
                    btn.classList.remove('on');
                }
            }
        } else if (data.id && data.state !== undefined) {
            let btn = document.getElementById(`btn${data.id}`);
            if (data.state) {
                btn.classList.add('on');
            } else {
                btn.classList.remove('on');
            }
        }
    } catch (e) {
        console.error("Error parsing message:", e);
    }
};

document.getElementById('btn1').onclick = sendChange("toggle", 1, -1);  
document.getElementById('btn2').onclick = sendChange("toggle", 2, -1); 
document.getElementById('btn3').onclick = sendChange("toggle", 3, -1); 
document.getElementById('btn4').onclick = sendChange("toggle", 4, -1); 
document.getElementById('btn5').onclick = sendChange("toggle", 5, -1); 
document.getElementById('btn6').onclick = sendChange("toggle", 6, -1); 