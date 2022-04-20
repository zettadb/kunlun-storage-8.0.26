set session sql_log_bin=0;

delimiter ;;
-- errcode: >0: success, the NO. of seq values fetched; -1: sequence not found; -2: sequence value consumed; -3: argument error
CREATE PROCEDURE kunlun_sysdb.seq_reserve_vals(dbname varchar(64), seqname varchar(64), nvals int , OUT newstart bigint, OUT newval bigint, OUT errcode int)
    MODIFIES SQL DATA
    SQL SECURITY INVOKER
cur_proc: BEGIN
    DECLARE step INT DEFAULT 0;
    DECLARE curval BIGINT DEFAULT NULL;
    DECLARE start_value BIGINT DEFAULT 0;
    DECLARE max_value BIGINT DEFAULT 0;
    DECLARE min_value BIGINT DEFAULT 0;
    DECLARE do_cycle TINYINT DEFAULT 0;
	DECLARE i INT DEFAULT 0;

	if nvals < 1 then
		set errcode = -3;
		set newval = NULL;
		set newstart = NULL;
		leave cur_proc;
	end if;

    set @dbname =dbname;
    set @seqname = seqname;
    set @nvals = nvals;
    SET @sql1 = '';
	SET errcode = 0;
	SET @ngot = 1;
	set newstart = NULL;
	-- curval is the max alloced value
    SET @sql1 = 'select curval, start, step, max_value, min_value, do_cycle from kunlun_sysdb.sequences where db=? and name=? for update into @curval, @start_value, @step, @max_value, @min_value, @do_cycle';
    PREPARE stmt1 FROM @sql1;

    EXECUTE stmt1 USING @dbname, @seqname;
	DEALLOCATE PREPARE stmt1;

	if @step = 0 or @step IS NULL then
		set errcode = -1;
		set newval = NULL;
		leave cur_proc;
	end if;

	IF @step > 0 THEN
		IF @curval <= -9223372036854775808 THEN
			set newval = @start_value;
			set @nvals = @nvals - 1;
			set newstart = newval;
    	ELSEIF @curval > @max_value - @step THEN
			IF @do_cycle THEN
				set newval = @min_value;
				set @nvals = @nvals - 1;
				set newstart = newval;
			ELSE
				set newval = NULL;
				set errcode = -2;
				LEAVE cur_proc;
			END IF;
		ELSE
			set newval = @curval;
			set newstart = newval + @step;
			SET @ngot = 0;
		END IF;

		set @i = floor((@max_value - newval) / @step);
		set @i = if(@i>@nvals, @nvals, @i);
		set newval = newval + @i*@step;
		set errcode = @i + @ngot;
	ELSE
		-- @step < 0
		IF @curval <= -9223372036854775808 THEN
			set newval = @start_value;
			set @nvals = @nvals - 1;
			set newstart = newval;
    	ELSEIF @curval < @min_value - @step THEN
			IF @do_cycle THEN
				set newval = @max_value;
				set @nvals = @nvals - 1;
				set newstart = newval;
			ELSE
				set newval = NULL;
				set errcode = -2;
				LEAVE cur_proc;
			END IF;
		ELSE
			set newval = @curval;
			set newstart = newval + @step;
			SET @ngot = 0;
		END IF;

		set @i = floor((@min_value - newval) / @step);
		set @i = if(@i>@nvals, @nvals, @i);
		set newval = newval + @i*@step;
		set errcode = @i+@ngot;
	END IF;

	if newval IS NOT NULL then
		set @sql2 = 'update kunlun_sysdb.sequences set curval = ? where db=? and name=?';
		PREPARE stmt2 FROM @sql2;
		set @newval = newval;
    	EXECUTE stmt2 USING @newval, @dbname, @seqname;

		DEALLOCATE PREPARE stmt2;
	end if;
END;;

-- usage:
--  call kunlun_sysdb.reserve_seq(
--		CONVERT('[{"seqrelid":1171460, "dbname": "abc_$$_public", "seqname":"s4", "nvals":10}]', JSON),
--		@res);
--  SELECT @res;
CREATE PROCEDURE kunlun_sysdb.reserve_seq(param JSON, OUT result JSON)
BEGIN
    DECLARE len INT;
    DECLARE i INT DEFAULT 0;
    DECLARE cur JSON;
    DECLARE seqrelid BIGINT;
    DECLARE dbid BIGINT;
    DECLARE dbname VARCHAR(64);
    DECLARE seqname VARCHAR(64);
    DECLARE nvals INT;
    DECLARE newstart BIGINT;
    DECLARE newval BIGINT;
    DECLARE retcode INT;

    SET len = JSON_LENGTH(param);
    SET result = JSON_ARRAY();
    WHILE i<len DO
        SET cur = JSON_EXTRACT(param, concat('$[',i,']'));
        SET dbid = JSON_EXTRACT(cur, "$.dbid");
        SET seqrelid = JSON_EXTRACT(cur, "$.seqrelid");
        SET dbname = JSON_UNQUOTE(JSON_EXTRACT(cur, "$.dbname"));
        SET seqname = JSON_UNQUOTE(JSON_EXTRACT(cur, "$.seqname"));
        SET nvals = JSON_UNQUOTE(JSON_EXTRACT(cur, "$.nvals"));
        SET newstart = 0, newval=0, retcode=0;
        CALL kunlun_sysdb.seq_reserve_vals(dbname, seqname, nvals, newstart, newval, retcode);
        SET result = JSON_ARRAY_APPEND(result, '$', JSON_OBJECT("dbid", dbid,
          			"seqrelid", seqrelid,
                "newstart", newstart,
                "newval", newval,
                "retcode", retcode)
        );
        SET i = i + 1;
    END WHILE;
END;;
