from django.conf.urls import url
from fdfs import views

urlpatterns = [
    url(r'^login.html$',views.login),
    url(r'^index.html$',views.index),
    url(r'^myfile.html$',views.file),
    url(r'^sharefile.html$',views.share),
    url(r'^uploadfile.html$',views.upload),
    url(r'^logout.jsp$',views.logout),
]
