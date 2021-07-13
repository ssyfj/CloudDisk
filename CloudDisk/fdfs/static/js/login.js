function turnRegister(){
	document.getElementById("loginForm").hidden=true;
	document.getElementById("loginForm").reset();
	document.getElementById("registerForm").hidden=false;
}

function GetLoginPostUrlPath(addr){
	var url = document.location.toString();
	var arr = url.split("/");
	var responeUrl = "";
	for(var i = 0; i<arr.length-1; i++){
	    responeUrl += arr[i]+"/";
	}
	responeUrl += addr;
	return responeUrl;
}

$("#submitLogin").click(function(){
    var targetUrl = GetLoginPostUrlPath("login.jsp");
    var user = $("#log_name").value();
    var pwd = $("#log_pwd").value();

    var data = JSON.stringify({
                    "user": user,
                    "pwd": $.md5(pwd),
                });

    console.log(data);
    alert(targetUrl);
    alert(data);
    $.ajax({
        type : 'post',
        url : targetUrl, 
        cache : false,
        data : data,  //重点必须为一个变量如：data
        dataType : 'json', 
        success : function(data){      
          alert('success');
        },
        error : function(){ 
         alert("请求失败")
        }
    });
});


