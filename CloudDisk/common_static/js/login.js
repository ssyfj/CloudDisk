function turnRegister(){
	document.getElementById("loginForm").hidden=true;
	document.getElementById("loginForm").reset();
	document.getElementById("registerForm").hidden=false;
}

$("#submitLogin").click(function(){
    var targetUrl = GetPostUrlPath("login.jsp");
    var user = $("#log_name").val();	
    var pwd = $("#log_pwd").val();

    if(user.length < 4){
		alert("username must granter than 4!");
		return;
    }

    if(pwd.length < 9){
		alert("password must granter than 9!");
		return;
    }

    var data = JSON.stringify({
                    "user": user,
                    "pwd": $.md5(pwd),
                });

    $.ajax({
        type : 'post',
        url : targetUrl, 
        cache : false,
        data : data,  
        dataType : 'json', 
        success : function(data){
        	//开始解析返回码
        	if(data["code"] === "000"){	//登录成功
        		$.cookie("set", {
					duration: 1,
					name: 'user',
					value: user
				});
        		$.cookie("set", {
					duration: 1,
					name: 'token',
					value: data["token"]
				});
        		window.location.href=GetRedirectNextUrl();
        	}else{						//登录失败
        		alert("登录失败");
        	}
        },
        error : function(){ 
			alert("请求失败")
        }
    });
});


function validatorTel(content){
    eval("var reg = /^1[34578]\\d{9}$/;");
    return RegExp(reg).test(content);
}


function validatorEmail(content){
	eval("var reg = /^([\.a-zA-Z0-9_-])+@([a-zA-Z0-9_-])+(\.[a-zA-Z0-9_-])+/;");
    return RegExp(reg).test(content);
}

$("#submitReg").click(function(){
    var targetUrl = GetPostUrlPath("register.jsp");
    var username = $("#username").val();	
    var nickname = $("#nickname").val();
    var password = $("#password").val();
    var phone = $("#phone").val();
    var email = $("#email").val();

    if(username.length < 4){
    	alert("username length must granter than 4！");
    	return;
    }

    if(nickname.length < 4){
    	alert("nickname length must granter than 4！");
    	return;
    }

    if(password.length < 9){
    	alert("password length must granter than 4！");
    	return;
    }

    if(validatorTel(phone) == false){
    	alert("phone format error!");
    	return;
    }

    if(validatorEmail(email) == false){
    	alert("email format error!");
    	return;
    }

    var data = JSON.stringify({
                    "username" : username,
                    "nickname" : nickname,
                    "password" : $.md5(password),
                    "phone" : phone,
                    "email" : email
                });

    $.ajax({
        type : 'post',
        url : targetUrl, 
        cache : false,
        data : data,  
        dataType : 'json', 
        success : function(data){
        	//开始解析返回码
        	if(data["code"] === "002"){	//登录成功
        		alert("注册成功，请登录！");
        		window.location.href=GetPostUrlPath("login.html");
        	}else{						//登录失败
        		alert(data["message"]);
        	}
        },
        error : function(){ 
			alert("请求失败")
        }
    });
});
