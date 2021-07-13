from django.db import models


class AuthGroup(models.Model):
    name = models.CharField(unique=True, max_length=150)

    class Meta:
        managed = False
        db_table = 'auth_group'


class AuthGroupPermissions(models.Model):
    id = models.BigAutoField(primary_key=True)
    group = models.ForeignKey(AuthGroup, models.DO_NOTHING)
    permission = models.ForeignKey('AuthPermission', models.DO_NOTHING)

    class Meta:
        managed = False
        db_table = 'auth_group_permissions'
        unique_together = (('group', 'permission'),)


class AuthPermission(models.Model):
    name = models.CharField(max_length=255)
    content_type = models.ForeignKey('DjangoContentType', models.DO_NOTHING)
    codename = models.CharField(max_length=100)

    class Meta:
        managed = False
        db_table = 'auth_permission'
        unique_together = (('content_type', 'codename'),)


class AuthUser(models.Model):
    password = models.CharField(max_length=128)
    last_login = models.DateTimeField(blank=True, null=True)
    is_superuser = models.IntegerField()
    username = models.CharField(unique=True, max_length=150)
    first_name = models.CharField(max_length=150)
    last_name = models.CharField(max_length=150)
    email = models.CharField(max_length=254)
    is_staff = models.IntegerField()
    is_active = models.IntegerField()
    date_joined = models.DateTimeField()

    class Meta:
        managed = False
        db_table = 'auth_user'


class AuthUserGroups(models.Model):
    id = models.BigAutoField(primary_key=True)
    user = models.ForeignKey(AuthUser, models.DO_NOTHING)
    group = models.ForeignKey(AuthGroup, models.DO_NOTHING)

    class Meta:
        managed = False
        db_table = 'auth_user_groups'
        unique_together = (('user', 'group'),)


class AuthUserUserPermissions(models.Model):
    id = models.BigAutoField(primary_key=True)
    user = models.ForeignKey(AuthUser, models.DO_NOTHING)
    permission = models.ForeignKey(AuthPermission, models.DO_NOTHING)

    class Meta:
        managed = False
        db_table = 'auth_user_user_permissions'
        unique_together = (('user', 'permission'),)


class DjangoAdminLog(models.Model):
    action_time = models.DateTimeField()
    object_id = models.TextField(blank=True, null=True)
    object_repr = models.CharField(max_length=200)
    action_flag = models.PositiveSmallIntegerField()
    change_message = models.TextField()
    content_type = models.ForeignKey('DjangoContentType', models.DO_NOTHING, blank=True, null=True)
    user = models.ForeignKey(AuthUser, models.DO_NOTHING)

    class Meta:
        managed = False
        db_table = 'django_admin_log'


class DjangoContentType(models.Model):
    app_label = models.CharField(max_length=100)
    model = models.CharField(max_length=100)

    class Meta:
        managed = False
        db_table = 'django_content_type'
        unique_together = (('app_label', 'model'),)


class DjangoMigrations(models.Model):
    id = models.BigAutoField(primary_key=True)
    app = models.CharField(max_length=255)
    name = models.CharField(max_length=255)
    applied = models.DateTimeField()

    class Meta:
        managed = False
        db_table = 'django_migrations'


class DjangoSession(models.Model):
    session_key = models.CharField(primary_key=True, max_length=40)
    session_data = models.TextField()
    expire_date = models.DateTimeField()

    class Meta:
        managed = False
        db_table = 'django_session'


class FileInfo(models.Model):
    id = models.BigAutoField(primary_key=True)
    file_id = models.CharField(max_length=256)
    md5 = models.CharField(max_length=200)
    url = models.CharField(max_length=512)
    size = models.BigIntegerField(blank=True, null=True)
    count = models.IntegerField(blank=True, null=True)
    type = models.CharField(max_length=20, blank=True, null=True)

    class Meta:
        managed = False
        db_table = 'file_info'


class ShareFileList(models.Model):
    id = models.BigAutoField(primary_key=True)
    user = models.CharField(max_length=128)
    md5 = models.CharField(max_length=200)
    create_time = models.DateTimeField()
    file_name = models.CharField(max_length=128)
    pv = models.IntegerField(blank=True, null=True)

    class Meta:
        managed = False
        db_table = 'share_file_list'


class UserFileCount(models.Model):
    id = models.BigAutoField(primary_key=True)
    user = models.CharField(max_length=128)
    count = models.IntegerField(blank=True, null=True)

    class Meta:
        managed = False
        db_table = 'user_file_count'


class UserFileList(models.Model):
    id = models.BigAutoField(primary_key=True)
    user = models.CharField(max_length=128)
    md5 = models.CharField(max_length=200)
    create_time = models.DateTimeField()
    file_name = models.CharField(max_length=128)
    shared_status = models.IntegerField(blank=True, null=True)
    pv = models.IntegerField(blank=True, null=True)

    class Meta:
        managed = False
        db_table = 'user_file_list'


class UserInfo(models.Model):
    id = models.BigAutoField(primary_key=True)
    user_name = models.CharField(unique=True, max_length=128)
    nick_name = models.CharField(unique=True, max_length=128)
    password = models.CharField(max_length=128)
    phone = models.CharField(max_length=15)
    create_time = models.DateTimeField()
    email = models.CharField(max_length=100, blank=True, null=True)

    class Meta:
        managed = False
        db_table = 'user_info'
